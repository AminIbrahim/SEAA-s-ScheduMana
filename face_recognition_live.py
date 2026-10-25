
import cv2
import numpy as np
import os

DETECTOR_MODEL = "face_detection_yunet_2023mar.onnx"
RECOGNIZER_MODEL = "face_recognition_sface_2021dec.onnx"
FACES_FOLDER = "faces"

# Load saved face profiles
known_faces = {}

for filename in os.listdir(FACES_FOLDER):
    if filename.endswith(".npy"):
        name = os.path.splitext(filename)[0]
        known_faces[name] = np.load(
            os.path.join(FACES_FOLDER, filename)
        ).astype(np.float32).flatten()

if not known_faces:
    print("No enrolled faces found. Run enroll_face.py first.")
    raise SystemExit

# Load the AI models
detector = cv2.FaceDetectorYN.create(
    DETECTOR_MODEL, "", (320, 320), 0.8, 0.3, 5000
)

recognizer = cv2.FaceRecognizerSF.create(
    RECOGNIZER_MODEL, ""
)

camera = cv2.VideoCapture(0)

if not camera.isOpened():
    print("Could not open camera.")
    raise SystemExit

# Starting threshold for cosine similarity; we'll tune it with testing
THRESHOLD = 0.363

print("Live recognition started. Press Q to quit.")

while True:
    success, frame = camera.read()

    if not success:
        print("Could not read camera.")
        break

    frame = cv2.flip(frame, 1)
    height, width = frame.shape[:2]
    detector.setInputSize((width, height))

    _, faces = detector.detect(frame)

    if faces is not None:
        for face in faces:
            x, y, w, h = face[:4].astype(int)

            aligned = recognizer.alignCrop(frame, face)
            embedding = recognizer.feature(aligned).flatten()

            # Compare this face with every enrolled profile
            best_name = "UNKNOWN"
            best_score = -1.0

            for name, saved_embedding in known_faces.items():
                score = float(
                    np.dot(embedding, saved_embedding)
                    / (
                        np.linalg.norm(embedding)
                        * np.linalg.norm(saved_embedding)
                        + 1e-10
                    )
                )

                if score > best_score:
                    best_score = score
                    best_name = name

            if best_score < THRESHOLD:
                label = "UNKNOWN"
                box_color = (0, 0, 255)
            else:
                label = best_name
                box_color = (0, 255, 0)

            cv2.rectangle(
                frame, (x, y), (x + w, y + h), box_color, 2
            )

            cv2.putText(
                frame,
                f"{label} ({best_score:.2f})",
                (x, max(y - 10, 20)),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                box_color,
                2
            )

    cv2.imshow("Home Security - Face Recognition", frame)

    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

camera.release()
cv2.destroyAllWindows()
