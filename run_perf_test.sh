gst-launch-1.0 videotestsrc num-buffers=300 ! \
  video/x-raw,format=NV12,width=640,height=480 ! \
  rknnfilter model-path=/home/cat/memoark/gst-rknn-filter/model/yolov5.rknn model-type=yolov5 \
    label-path=/home/cat/memoark/gst-rknn-filter/model/coco_80_labels_list.txt show-fps=true ! \
  videoconvert ! autovideosink
