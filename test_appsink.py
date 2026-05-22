#!/usr/bin/env python3
import sys
import numpy as np
import gi

gi.require_version('Gst', '1.0')
from gi.repository import Gst, GObject, GLib

Gst.init(None)

class TestApp:
    def __init__(self):
        self.frame_count = 0

        pipeline_str = (
            'videotestsrc is-live=true pattern=smpte ! '
            'video/x-raw,width=960,height=810,format=RGB ! '
            'appsink name=appsink emit-signals=true sync=false'
        )

        self.pipeline = Gst.parse_launch(pipeline_str)
        self.appsink = self.pipeline.get_by_name('appsink')
        self.appsink.connect('new-sample', self.on_new_sample)

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
        print(f"Frame {self.frame_count} received: {width}x{height}")

        return Gst.FlowReturn.OK

    def run(self):
        self.pipeline.set_state(Gst.State.PLAYING)
        print("Starting test...")

        loop = GLib.MainLoop()
        try:
            loop.run()
        except KeyboardInterrupt:
            print("\nStopping...")
            self.pipeline.set_state(Gst.State.NULL)

if __name__ == '__main__':
    app = TestApp()
    app.run()