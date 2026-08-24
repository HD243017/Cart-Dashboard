import cv2
from PyQt5.QtGui import QImage, QPixmap
from PyQt5.QtCore import Qt

COLOR_MAP = {
    'r1': (0, 0, 255),
    'g1': (0, 255, 0),
    'y1': (0, 215, 255)
}
LABEL_MAP = {
    'r1': 'red',
    'g1': 'green',
    'y1': 'yellow'
}

def draw_detections(cv_img, detections):
    if not detections:
        return cv_img

    img_h, img_w = cv_img.shape[:2]
    scale_x = img_w / 96.0
    scale_y = img_h / 96.0
    box_size = 24

    for item in detections:
        label = item.get('label', '')
        score = item.get('value', 0.0)
        cx = int(item.get('x', 0) * scale_x)
        cy = int(item.get('y', 0) * scale_y)

        color = COLOR_MAP.get(label, (255, 255, 0))
        display_name = LABEL_MAP.get(label, label)

        x1 = max(0, int(cx - box_size / 2))
        y1 = max(0, int(cy - box_size / 2))
        x2 = min(img_w, int(cx + box_size / 2))
        y2 = min(img_h, int(cy + box_size / 2))

        cv2.rectangle(cv_img, (x1, y1), (x2, y2), color, 2)
        text = f"{display_name} ({score:.2f})"
        cv2.putText(cv_img, text, (x1, max(15, y1 - 5)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, color, 1, cv2.LINE_AA)

    return cv_img

def cv_to_pixmap(cv_img, target_w=640, target_h=480) -> QPixmap:
    rgb = cv2.cvtColor(cv_img, cv2.COLOR_BGR2RGB)
    h, w, ch = rgb.shape
    qt_img = QImage(rgb.data, w, h, ch * w, QImage.Format_RGB888)
    return QPixmap.fromImage(qt_img).scaled(target_w, target_h, Qt.KeepAspectRatio)
