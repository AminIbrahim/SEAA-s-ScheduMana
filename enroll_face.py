
import cv2
import numpy as np
import os

# Model files
DETECTOR_MODEL = "face_detection_yunet_2023mar.onnx"
RECOGNIZER_MODEL = "face_recognition_sface_2021dec.onnx"

# Folder for saved face data
os.makedirs("faces", exist_ok=True)

name = input("Enter the person's name to enroll: ").strip()

if not name:
    print("Please enter a name.")
    raise SystemExit

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

embeddings = []
print("Look at the camera. Press SPACE to save a face sample.")
print("Capture 10 samples from slightly different angles.")
print("Press Q to cancel.")

while len(embeddings) < 10:
    success, frame = camera.read()

    if not success:
        print("Could not read camera.")
        break

    frame = cv2.flip(frame, 1)
    height, width = frame.shape[:2]
    detector.setInputSize((width, height))

    _, faces = detector.detect(frame)

    cv2.putText(
        frame,
        f"Samples: {len(embeddings)}/10 | SPACE = capture | Q = quit",
        (10, 30),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.6,
        (0, 255, 0),
        2
    )

    if faces is not None and len(faces) > 0:
        # Use the largest detected face
        face = max(faces, key=lambda f: f[2] * f[3])
        x, y, w, h = face[:4].astype(int)

        cv2.rectangle(frame, (x, y), (x + w, y + h), (0, 255, 0), 2)

    cv2.imshow("Enroll Face", frame)
    key = cv2.waitKey(1) & 0xFF

    if key == ord("q"):
        break

    if key == 32:  # SPACE
        if faces is None or len(faces) == 0:
            print("No face detected. Try again.")
            continue

        face = max(faces, key=lambda f: f[2] * f[3])
        aligned = recognizer.alignCrop(frame, face)
        embedding = recognizer.feature(aligned).flatten()

        embeddings.append(embedding)
        print(f"Captured sample {len(embeddings)}/10")

camera.release()
cv2.destroyAllWindows()

if len(embeddings) == 10:
    safe_name = "".join(
        c for c in name if c.isalnum() or c in ("_", "-")
    )

    if not safe_name:
        print("Invalid name.")
        raise SystemExit

    output_path = os.path.join("faces", safe_name + ".npy")
    np.save(output_path, np.mean(embeddings, axis=0))

    print(f"Enrollment complete! Saved to {output_path}")
else:
    print("Enrollment cancelled. No new profile saved.")
