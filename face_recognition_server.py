import cv2
import numpy as np
import os
import json
import urllib.request
import urllib.error
import time

# ==========================================
# CONFIGURATION
# ==========================================

DETECTOR_MODEL = "face_detection_yunet_2023mar.onnx"
RECOGNIZER_MODEL = "face_recognition_sface_2021dec.onnx"
FACES_FOLDER = "faces"

SERVER_URL = "http://127.0.0.1:5000/face"
SERVER_STATUS_URL = "http://127.0.0.1:5000/status"
RESET_URL = "http://127.0.0.1:5000/api/access/reset"

THRESHOLD = 0.363

# How frequently to check whether the system is reset.
STATUS_CHECK_INTERVAL = 0.3


# ==========================================
# LOAD ENROLLED FACES
# ==========================================

known_faces = {}

if not os.path.isdir(FACES_FOLDER):
    raise SystemExit(
        f"Could not find the '{FACES_FOLDER}' folder."
    )

for filename in os.listdir(FACES_FOLDER):
    if filename.endswith(".npy"):
        name = os.path.splitext(filename)[0]

        known_faces[name] = np.load(
            os.path.join(FACES_FOLDER, filename)
        ).astype(np.float32).flatten()

if not known_faces:
    raise SystemExit(
        "No enrolled faces found. Run enroll_face.py first."
    )

print("Enrolled faces:")
for name in known_faces:
    print(f" - {name}")


# ==========================================
# LOAD FACE DETECTION MODELS
# ==========================================

if not os.path.isfile(DETECTOR_MODEL):
    raise SystemExit(
        f"Could not find detector model: {DETECTOR_MODEL}"
    )

if not os.path.isfile(RECOGNIZER_MODEL):
    raise SystemExit(
        f"Could not find recognizer model: {RECOGNIZER_MODEL}"
    )

detector = cv2.FaceDetectorYN.create(
    DETECTOR_MODEL,
    "",
    (320, 320),
    0.8,
    0.3,
    5000
)

recognizer = cv2.FaceRecognizerSF.create(
    RECOGNIZER_MODEL,
    ""
)


# ==========================================
# SERVER COMMUNICATION
# ==========================================

def send_status(status, name=None):
    """Send a face-recognition result to Flask."""

    payload = {
        "status": status,
        "name": name
    }

    data = json.dumps(payload).encode("utf-8")

    request = urllib.request.Request(
        SERVER_URL,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST"
    )

    try:
        with urllib.request.urlopen(
            request,
            timeout=1
        ) as response:

            result = response.read().decode("utf-8")
            print(f"Face status: {status} | Server: {result}")

    except (urllib.error.URLError, TimeoutError) as error:
        print("Could not send status to Flask:", error)


def get_server_status():
    """
    Get the current security state from Flask.

    Returns a dictionary when successful, or None
    if the server cannot be reached.
    """

    try:
        with urllib.request.urlopen(
            SERVER_STATUS_URL,
            timeout=1
        ) as response:

            return json.loads(
                response.read().decode("utf-8")
            )

    except (
        urllib.error.URLError,
        TimeoutError,
        json.JSONDecodeError
    ) as error:

        print("Could not check Flask status:", error)
        return None


def reset_security():
    """Reset Flask security state and resume facial recognition."""

    request = urllib.request.Request(
        RESET_URL,
        data=json.dumps({}).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST"
    )

    try:
        with urllib.request.urlopen(
            request,
            timeout=3
        ) as response:

            result = response.read().decode("utf-8")
            print("Security reset:", result)
            return True

    except (urllib.error.URLError, TimeoutError) as error:
        print("Could not reset security:", error)
        return False


# ==========================================
# KEYBOARD CONTROLS
# ==========================================

def handle_reset_key():
    """
    Reset Flask when R is pressed.
    Keep the camera and recognition models running.
    """

    global recognition_paused
    global last_sent
    global last_status_check
    global server_available

    print("\nResetting security and resuming face recognition...")

    if reset_security():

        recognition_paused = False
        last_sent = None
        last_status_check = 0
        server_available = True

        print("Security reset. Face recognition resumed.\n")

    else:
        print("Reset failed. Check that Flask is running.\n")


# ==========================================
# CAMERA
# ==========================================

camera = cv2.VideoCapture(0)

if not camera.isOpened():
    raise SystemExit("Could not open camera.")

last_sent = None
last_status_check = 0

recognition_paused = False
server_available = True


# ==========================================
# MAIN PROGRAM
# ==========================================

print("\nFace recognition connected to Flask.")
print("Access will remain active until the system is reset.")
print("Press R to reset security and resume recognition.")
print("Press Q to close the program.\n")


try:
    while True:

        # --------------------------------------
        # READ CAMERA FRAME
        # --------------------------------------

        success, frame = camera.read()

        if not success:
            print("Could not read camera.")
            time.sleep(0.1)
            continue

        frame = cv2.flip(frame, 1)

        height, width = frame.shape[:2]

        # --------------------------------------
        # CHECK FLASK SECURITY STATE
        # --------------------------------------

        current_time = time.monotonic()

        if (
            current_time - last_status_check
            >= STATUS_CHECK_INTERVAL
        ):

            server_state = get_server_status()
            last_status_check = current_time

            if server_state is None:

                server_available = False

            else:

                server_available = True

                recognition_enabled = server_state.get(
                    "recognition_enabled",
                    True
                )

                if not recognition_enabled:

                    if not recognition_paused:
                        print(
                            "\nACCESS GRANTED."
                            "\nFacial recognition is now PAUSED."
                            "\nPress R to reset security and resume.\n"
                        )

                    recognition_paused = True

                else:

                    if recognition_paused:
                        print(
                            "\nSystem reset detected."
                            "\nResuming facial recognition.\n"
                        )

                        # Allow the next face state to be sent.
                        last_sent = None

                    recognition_paused = False

        # --------------------------------------
        # PAUSED STATE
        # --------------------------------------

        if recognition_paused:

            cv2.putText(
                frame,
                "ACCESS GRANTED",
                (30, 50),
                cv2.FONT_HERSHEY_SIMPLEX,
                1.0,
                (0, 255, 0),
                2
            )

            cv2.putText(
                frame,
                "RECOGNITION PAUSED",
                (30, 95),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.8,
                (0, 255, 255),
                2
            )

            cv2.putText(
                frame,
                "Press R to reset and resume",
                (30, 135),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                (255, 255, 255),
                2
            )

            cv2.imshow(
                "Face Recognition + Flask",
                frame
            )

            key = cv2.waitKey(1) & 0xFF

            if key == ord("r"):
                handle_reset_key()

            elif key == ord("q"):
                break

            continue

        # --------------------------------------
        # SERVER CONNECTION WARNING
        # --------------------------------------

        if not server_available:

            cv2.putText(
                frame,
                "SERVER CONNECTION LOST",
                (20, 40),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                (0, 0, 255),
                2
            )

            cv2.imshow(
                "Face Recognition + Flask",
                frame
            )

            key = cv2.waitKey(1) & 0xFF

            if key == ord("r"):
                handle_reset_key()

            elif key == ord("q"):
                break

            continue

        # --------------------------------------
        # FACE DETECTION
        # --------------------------------------

        detector.setInputSize((width, height))

        _, faces = detector.detect(frame)

        status = "NO_FACE"
        person = None

        # --------------------------------------
        # NO FACE
        # --------------------------------------

        if faces is None or len(faces) == 0:

            status = "NO_FACE"

            cv2.putText(
                frame,
                "SYSTEM SECURED",
                (20, 40),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.8,
                (0, 255, 0),
                2
            )

        # --------------------------------------
        # MULTIPLE FACES
        # --------------------------------------

        elif len(faces) > 1:

            status = "MULTIPLE_PEOPLE"

            cv2.putText(
                frame,
                "ONE PERSON AT A TIME",
                (20, 40),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.8,
                (0, 165, 255),
                2
            )

            # Outline every detected face.
            for face in faces:

                x, y, w, h = face[:4].astype(int)

                cv2.rectangle(
                    frame,
                    (x, y),
                    (x + w, y + h),
                    (0, 165, 255),
                    2
                )

        # --------------------------------------
        # EXACTLY ONE FACE
        # --------------------------------------

        else:

            face = faces[0]

            x, y, w, h = face[:4].astype(int)

            # Align and extract the face embedding.
            aligned = recognizer.alignCrop(
                frame,
                face
            )

            embedding = recognizer.feature(
                aligned
            ).flatten()

            best_name = "UNKNOWN"
            best_score = -1.0

            # Compare against every enrolled face.
            for name, saved in known_faces.items():

                score = float(
                    np.dot(embedding, saved)
                    / (
                        np.linalg.norm(embedding)
                        * np.linalg.norm(saved)
                        + 1e-10
                    )
                )

                if score > best_score:
                    best_name = name
                    best_score = score

            # Determine whether the face is recognized.
            if best_score >= THRESHOLD:

                status = "WELCOME"
                person = best_name

                label = f"{best_name}: {best_score:.2f}"
                color = (0, 255, 0)

            else:

                status = "UNKNOWN"
                label = f"UNKNOWN: {best_score:.2f}"
                color = (0, 0, 255)

            cv2.rectangle(
                frame,
                (x, y),
                (x + w, y + h),
                color,
                2
            )

            cv2.putText(
                frame,
                label,
                (x, max(y - 10, 20)),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                color,
                2
            )

        # --------------------------------------
        # SEND ONLY CHANGED FACE STATES
        # --------------------------------------

        current = (status, person)

        if current != last_sent:

            send_status(status, person)
            last_sent = current

        # --------------------------------------
        # DISPLAY CAMERA
        # --------------------------------------

        cv2.imshow(
            "Face Recognition + Flask",
            frame
        )

        key = cv2.waitKey(1) & 0xFF

        if key == ord("r"):
            handle_reset_key()

        elif key == ord("q"):
            break

finally:

    camera.release()
    cv2.destroyAllWindows()

    print("\nFace recognition program stopped.")
