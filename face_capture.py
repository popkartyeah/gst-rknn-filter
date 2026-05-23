#!/usr/bin/env python3
import sys
import os
import math
import time
import ctypes
import numpy as np
import gi

gi.require_version('Gst', '1.0')
from gi.repository import Gst, GObject, GLib

Gst.init(None)

# Define FaceMetaResult structure matching C code
class FaceMetaResult(ctypes.Structure):
    _fields_ = [
        ("face_count", ctypes.c_int),
        ("names", ctypes.c_char * 16 * 128),
        ("lefts", ctypes.c_int * 128),
        ("rights", ctypes.c_int * 128),
        ("tops", ctypes.c_int * 128),
        ("bottoms", ctypes.c_int * 128),
        ("props", ctypes.c_float * 128)
    ]

# Load our rknn plugin library to access custom functions
librknn = None
try:
    librknn = ctypes.CDLL('/usr/local/lib/aarch64-linux-gnu/gstreamer-1.0/libgstrknn.so')
    print("Loaded libgstrknn.so successfully")
    
    # Setup the extract function
    librknn.gst_face_meta_extract.restype = ctypes.c_int
    librknn.gst_face_meta_extract.argtypes = [ctypes.c_void_p, ctypes.POINTER(FaceMetaResult)]
    
except Exception as e:
    print(f"Failed to load libgstrknn.so: {e}")

class FaceCropApp:
    def __init__(self, rtmp_url, model_path, output_dir="/tmp/faces"):
        self.rtmp_url = rtmp_url
        self.model_path = model_path
        self.output_dir = output_dir
        self.frame_count = 0
        self.save_count = 0
        self.frame_skip = 3
        self.frame_width = 960
        self.frame_height = 810

        os.makedirs(self.output_dir, exist_ok=True)

        pipeline_str = (
            f'rtmpsrc location={rtmp_url} ! '
            'flvdemux name=demux ! '
            'queue ! '
            'h264parse ! '
            'mppvideodec ! '
            'video/x-raw,format=NV12 ! '
            'videocrop top=0 bottom=270 left=480 right=480 ! '
            'video/x-raw,format=NV12,width=960,height=810 ! '
            f'rknnfilter model-path={self.model_path} model-type=retinaface ! '
            'video/x-raw,format=RGB ! '
            'appsink name=appsink emit-signals=true sync=false'
        )

        self.pipeline = Gst.parse_launch(pipeline_str)
        self.appsink = self.pipeline.get_by_name('appsink')
        self.appsink.connect('new-sample', self.on_new_sample)

    def calculate_distance(self, x1, y1, x2, y2):
        return math.sqrt((x2 - x1) ** 2 + (y2 - y1) ** 2)

    def find_nearest_face(self, faces, center_x, center_y):
        nearest = None
        min_dist = float('inf')
        for face in faces:
            face_center_x = (face.left + face.right) // 2
            face_center_y = (face.top + face.bottom) // 2
            dist = self.calculate_distance(face_center_x, face_center_y, center_x, center_y)
            if dist < min_dist:
                min_dist = dist
                nearest = face
        return nearest

    def crop_and_save_face(self, frame, face):
        x1 = max(0, face.left - 20)
        y1 = max(0, face.top - 20)
        x2 = min(self.frame_width, face.right + 20)
        y2 = min(self.frame_height, face.bottom + 20)

        face_crop = frame[y1:y2, x1:x2]

        if face_crop.size > 0:
            timestamp = int(time.time() * 1000)
            filename = f"{self.output_dir}/face_{timestamp}.jpg"
            import cv2
            rgb_crop = cv2.cvtColor(face_crop, cv2.COLOR_RGB2BGR)
            cv2.imwrite(filename, rgb_crop)
            self.save_count += 1
            print(f"[{self.save_count}] Saved: {filename} (face at {x1},{y1}-{x2},{y2})")

    def crop_and_save_face_dict(self, frame, face_dict):
        x1 = max(0, face_dict['left'] - 20)
        y1 = max(0, face_dict['top'] - 20)
        x2 = min(self.frame_width, face_dict['right'] + 20)
        y2 = min(self.frame_height, face_dict['bottom'] + 20)

        face_crop = frame[y1:y2, x1:x2]

        if face_crop.size > 0:
            timestamp = int(time.time() * 1000)
            filename = f"{self.output_dir}/face_{timestamp}.jpg"
            import cv2
            rgb_crop = cv2.cvtColor(face_crop, cv2.COLOR_RGB2BGR)
            cv2.imwrite(filename, rgb_crop)
            self.save_count += 1
            print(f"[{self.save_count}] Saved: {filename} (face at {x1},{y1}-{x2},{y2})")

    def on_new_sample(self, sink):
        sample = sink.emit('pull-sample')
        if not sample:
            return Gst.FlowReturn.OK

        buffer = sample.get_buffer()
        caps = sample.get_caps()

        structure = caps.get_structure(0)
        width = structure.get_value('width')
        height = structure.get_value('height')

        success, map_info = buffer.map(Gst.MapFlags.READ)
        if not success:
            return Gst.FlowReturn.OK

        frame = np.frombuffer(map_info.data, dtype=np.uint8).reshape(height, width, 3)
        buffer.unmap(map_info)

        self.frame_count += 1
        if self.frame_count % (self.frame_skip + 1) != 0:
            return Gst.FlowReturn.OK

        try:
            if librknn:
                # Get the C pointer to GstBuffer
                # In PyGObject, we can access the underlying GObject pointer
                buffer_ptr = None
                
                # Try multiple ways to get the C pointer
                if hasattr(buffer, '__gpointer__'):
                    buffer_ptr = int(buffer.__gpointer__)
                elif hasattr(buffer, '_gobject'):
                    # For some older PyGObject versions
                    buffer_ptr = ctypes.cast(id(buffer._gobject), ctypes.POINTER(ctypes.c_void_p))[0]
                else:
                    # Try using the hash as a fallback (not reliable)
                    buffer_ptr = hash(buffer)
                
                print(f"Frame {self.frame_count}: buffer pointer = {buffer_ptr}")
                
                # Prepare result structure
                result = FaceMetaResult()
                
                # Call the extract function
                ret = librknn.gst_face_meta_extract(ctypes.c_void_p(buffer_ptr), ctypes.byref(result))
                
                if ret > 0:
                    print(f"Frame {self.frame_count}: Found {ret} faces")
                    
                    center_x = self.frame_width // 2
                    center_y = self.frame_height // 2
                    
                    # Find nearest face
                    nearest_face = None
                    min_dist = float('inf')
                    
                    for i in range(result.face_count):
                        face_left = result.lefts[i]
                        face_right = result.rights[i]
                        face_top = result.tops[i]
                        face_bottom = result.bottoms[i]
                        face_prop = result.props[i]
                        
                        face_center_x = (face_left + face_right) // 2
                        face_center_y = (face_top + face_bottom) // 2
                        dist = math.sqrt((face_center_x - center_x) ** 2 + (face_center_y - center_y) ** 2)
                        
                        if dist < min_dist:
                            min_dist = dist
                            nearest_face = {
                                'left': face_left,
                                'right': face_right,
                                'top': face_top,
                                'bottom': face_bottom,
                                'prop': face_prop
                            }
                    
                    if nearest_face:
                        self.crop_and_save_face_dict(frame, nearest_face)
                elif ret == 0:
                    print(f"Frame {self.frame_count}: No face meta found")
                else:
                    print(f"Frame {self.frame_count}: Error extracting face meta: {ret}")
            else:
                print(f"Frame {self.frame_count}: librknn not loaded")
                
        except Exception as e:
            print(f"Error getting face meta: {e}")
            import traceback
            traceback.print_exc()

        return Gst.FlowReturn.OK

    def run(self):
        self.pipeline.set_state(Gst.State.PLAYING)
        print(f"Starting face detection and capture...")
        print(f"RTMP URL: {self.rtmp_url}")
        print(f"Model: {self.model_path}")
        print(f"Output directory: {self.output_dir}")

        loop = GLib.MainLoop()
        try:
            loop.run()
        except KeyboardInterrupt:
            print("\nStopping...")
            self.pipeline.set_state(Gst.State.NULL)

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python3 face_capture.py <rtmp_url> <model_path> [output_dir]")
        sys.exit(1)

    rtmp_url = sys.argv[1]
    model_path = sys.argv[2]
    output_dir = sys.argv[3] if len(sys.argv) > 3 else "/tmp/faces"

    app = FaceCropApp(rtmp_url, model_path, output_dir)
    app.run()