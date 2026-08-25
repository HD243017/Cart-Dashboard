import numpy as np
import pyqtgraph.opengl as gl
from PyQt5.QtWidgets import QWidget, QVBoxLayout, QSizePolicy


class Cart3DViewer(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        
        # 3D GL 뷰포트
        self.view = gl.GLViewWidget()
        self.view.setBackgroundColor('#181926')
        self.view.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.view.setCameraPosition(distance=16, elevation=25, azimuth=45)
        layout.addWidget(self.view)

        # 바닥 기준 그리드
        grid = gl.GLGridItem()
        grid.setSize(18, 18)
        grid.setSpacing(1, 1)
        grid.setColor((73, 77, 100, 60))
        self.view.addItem(grid)

        self._build_cart_model()

    def _build_cart_model(self):
        # 1. 카트 본체 (Z축 중심을 0으로 맞춤)
        verts = np.array([
            [-2.0, -1.2, -0.6], [ 2.0, -1.2, -0.6], [ 2.0,  1.2, -0.6], [-2.0,  1.2, -0.6],
            [-2.0, -1.2,  0.6], [ 2.0, -1.2,  0.6], [ 2.0,  1.2,  0.6], [-2.0,  1.2,  0.6]
        ])
        faces = np.array([
            [0, 1, 2], [0, 2, 3], # 바닥
            [4, 5, 6], [4, 6, 7], # 상판
            [0, 1, 5], [0, 5, 4], # 전면
            [2, 3, 7], [2, 7, 6], # 후면
            [1, 2, 6], [1, 6, 5], # 우측
            [0, 3, 7], [0, 7, 4]  # 좌측
        ])
        colors = np.array([[0.23, 0.51, 0.96, 0.85] for _ in range(12)])
        
        self.mesh_body = gl.GLMeshItem(
            vertexes=verts, faces=faces, faceColors=colors,
            drawEdges=True, edgeColor=(0.7, 0.8, 1.0, 1.0), smooth=False
        )
        self.view.addItem(self.mesh_body)

        # 2. 주행 방향 식별용 전방 헤드포인트 (그린)
        head_verts = np.array([
            [1.5, -0.6, 0.6], [2.0, -0.6, 0.6], [2.0, 0.6, 0.6], [1.5, 0.6, 0.6],
            [1.5, -0.6, 1.0], [2.0, -0.6, 1.0], [2.0, 0.6, 1.0], [1.5, 0.6, 1.0]
        ])
        head_colors = np.array([[0.65, 0.85, 0.58, 0.9] for _ in range(12)])
        self.mesh_head = gl.GLMeshItem(
            vertexes=head_verts, faces=faces, faceColors=head_colors,
            drawEdges=True, edgeColor=(1.0, 1.0, 1.0, 1.0)
        )
        self.view.addItem(self.mesh_head)

        # 3. 바퀴 4개 (섀시 기준 상대 위치)
        wheel_offsets = [
            ( 1.3,  1.3, -0.7),
            ( 1.3, -1.3, -0.7),
            (-1.3,  1.3, -0.7),
            (-1.3, -1.3, -0.7)
        ]
        self.wheels = []
        for ox, oy, oz in wheel_offsets:
            w_verts = np.array([
                [ox-0.4, oy-0.15, oz-0.2], [ox+0.4, oy-0.15, oz-0.2],
                [ox+0.4, oy+0.15, oz-0.2], [ox-0.4, oy+0.15, oz-0.2],
                [ox-0.4, oy-0.15, oz+0.2], [ox+0.4, oy-0.15, oz+0.2],
                [ox+0.4, oy+0.15, oz+0.2], [ox-0.4, oy+0.15, oz+0.2]
            ])
            w_colors = np.array([[0.1, 0.1, 0.15, 1.0] for _ in range(12)])
            wheel = gl.GLMeshItem(
                vertexes=w_verts, faces=faces, faceColors=w_colors,
                drawEdges=True, edgeColor=(0.4, 0.4, 0.5, 1.0)
            )
            self.view.addItem(wheel)
            self.wheels.append(wheel)

        self.cart_items = [self.mesh_body, self.mesh_head] + self.wheels

    def update_pose(self, yaw: float, pitch: float, roll: float):
        """
        yaw: Z축 회전 (헤딩 방향)
        pitch: Y축 회전 (앞뒤 기울기)
        roll: X축 회전 (좌우 롤링)
        """
        for item in self.cart_items:
            # 1. 이전 프레임의 변환 행렬 초기화
            item.resetTransform()
            
            # 2. 지상 기본 높이(Z축 오프셋) 유지
            item.translate(0, 0, 1.0)
            
            # 3. Euler 각도 회전 적용 (Yaw -> Pitch -> Roll 순서)
            item.rotate(float(yaw), 0, 0, 1)    # Yaw (Z축 기준)
            item.rotate(float(pitch), 0, 1, 0)  # Pitch (Y축 기준)
            item.rotate(float(roll), 1, 0, 0)   # Roll (X축 기준)

        # 4. OpenGL 뷰포트 강제 화면 재렌더링 트리거
        self.view.update()
