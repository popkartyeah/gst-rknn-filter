package main

/*
#cgo pkg-config: gstreamer-1.0 gstreamer-app-1.0
#cgo LDFLAGS: -L/home/cat/memoark/gst-rknn-filter/build/src -lgstrknn
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <stdlib.h>

typedef struct {
    int face_count;
    char names[128][16];
    int lefts[128];
    int rights[128];
    int tops[128];
    int bottoms[128];
    float props[128];
} FaceMetaResult;

extern int gst_face_meta_extract(gpointer buffer_ptr, FaceMetaResult* result);
*/
import "C"

import (
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"syscall"
	"time"

	"github.com/go-gst/go-gst/gst"
	gstapp "github.com/go-gst/go-gst/gst/app"
)

const (
	OutputDir   = "/tmp/faces_go"
	FrameSkip   = 3
	FrameWidth  = 960
	FrameHeight = 810
)

var saveCount int

type GoFaceMeta struct {
	face_count int32
	lefts      [128]int32
	rights     [128]int32
	tops       [128]int32
	bottoms    [128]int32
}

type FaceCropApp struct {
	rtmpURL    string
	modelPath  string
	frameCount int
	pipeline   *gst.Pipeline
	appsink    *gstapp.Sink
}

func newFaceCropApp(rtmpURL, modelPath string) *FaceCropApp {
	return &FaceCropApp{
		rtmpURL:   rtmpURL,
		modelPath: modelPath,
	}
}

func (app *FaceCropApp) run() error {
	gst.Init(nil)

	if err := os.MkdirAll(OutputDir, 0755); err != nil {
		return fmt.Errorf("failed to create output dir: %v", err)
	}

	pipelineStr := fmt.Sprintf(
		"rtmpsrc location=%s ! "+
			"flvdemux name=demux ! "+
			"queue ! "+
			"h264parse ! "+
			"mppvideodec ! "+
			"video/x-raw,format=NV12 ! "+
			"videocrop top=0 bottom=270 left=480 right=480 ! "+
			"video/x-raw,format=NV12,width=960,height=810 ! "+
			"rknnfilter model-path=%s model-type=retinaface ! "+
			"video/x-raw,format=RGB ! "+
			"appsink name=appsink sync=false",
		app.rtmpURL, app.modelPath,
	)

	fmt.Printf("Pipeline: %s\n", pipelineStr)

	var err error
	app.pipeline, err = gst.NewPipelineFromString(pipelineStr)
	if err != nil {
		return fmt.Errorf("failed to create pipeline: %v", err)
	}

	appsinkElement, err := app.pipeline.GetElementByName("appsink")
	if err != nil {
		return fmt.Errorf("failed to get appsink: %v", err)
	}

	app.appsink = gstapp.SinkFromElement(appsinkElement)
	if app.appsink == nil {
		return fmt.Errorf("failed to create appsink")
	}

	app.appsink.SetCallbacks(&gstapp.SinkCallbacks{
		NewSampleFunc: func(sink *gstapp.Sink) gst.FlowReturn {
			return app.onNewSample(sink)
		},
	})

	fmt.Println("Starting face detection and capture...")
	fmt.Printf("RTMP URL: %s\n", app.rtmpURL)
	fmt.Printf("Model: %s\n", app.modelPath)
	fmt.Printf("Output directory: %s\n", OutputDir)

	app.pipeline.SetState(gst.StatePlaying)

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	<-sigChan

	app.pipeline.SetState(gst.StateNull)
	app.pipeline.Unref()

	return nil
}

func (app *FaceCropApp) onNewSample(sink *gstapp.Sink) gst.FlowReturn {
	app.frameCount++

	if app.frameCount%(FrameSkip+1) != 0 {
		return gst.FlowOK
	}

	sample := sink.PullSample()
	if sample == nil {
		fmt.Println("Warning: nil sample")
		return gst.FlowError
	}
	defer sample.Unref()

	buffer := sample.GetBuffer()
	if buffer == nil {
		fmt.Println("Warning: nil buffer")
		return gst.FlowError
	}

	mapInfo := buffer.Map(gst.MapRead)
	if mapInfo == nil {
		fmt.Println("Failed to map buffer")
		return gst.FlowError
	}
	defer buffer.Unmap()

	data := mapInfo.Data()
	dataBytes := C.GoBytes(data, C.int(mapInfo.Size()))

	faceMeta := app.getFaceMeta(buffer)
	if faceMeta != nil && faceMeta.face_count > 0 {
		fmt.Printf("Frame %d: Found %d faces\n", app.frameCount, faceMeta.face_count)

		centerX := FrameWidth / 2
		centerY := FrameHeight / 2
		minDist := int(^uint(0) >> 1)
		var nearestIdx int

		for i := 0; i < int(faceMeta.face_count); i++ {
			faceLeft := int(faceMeta.lefts[i])
			faceRight := int(faceMeta.rights[i])
			faceTop := int(faceMeta.tops[i])
			faceBottom := int(faceMeta.bottoms[i])

			faceCenterX := (faceLeft + faceRight) / 2
			faceCenterY := (faceTop + faceBottom) / 2

			dist := (faceCenterX-centerX)*(faceCenterX-centerX) + (faceCenterY-centerY)*(faceCenterY-centerY)
			if dist < minDist {
				minDist = dist
				nearestIdx = i
			}
		}

		faceLeft := int(faceMeta.lefts[nearestIdx])
		faceRight := int(faceMeta.rights[nearestIdx])
		faceTop := int(faceMeta.tops[nearestIdx])
		faceBottom := int(faceMeta.bottoms[nearestIdx])

		app.saveFaceRGB(dataBytes, faceLeft, faceRight, faceTop, faceBottom)
	}

	return gst.FlowOK
}

func (app *FaceCropApp) getFaceMeta(buffer *gst.Buffer) *GoFaceMeta {
	if buffer == nil {
		return nil
	}

	nativeBuffer := buffer.Instance()

	var cResult C.FaceMetaResult
	ret := C.gst_face_meta_extract((C.gpointer)(nativeBuffer), &cResult)

	if ret <= 0 {
		return nil
	}

	result := &GoFaceMeta{
		face_count: int32(cResult.face_count),
	}

	for i := 0; i < int(cResult.face_count) && i < 128; i++ {
		result.lefts[i] = int32(cResult.lefts[i])
		result.rights[i] = int32(cResult.rights[i])
		result.tops[i] = int32(cResult.tops[i])
		result.bottoms[i] = int32(cResult.bottoms[i])
	}

	return result
}

func (app *FaceCropApp) saveFaceRGB(rgbData []byte, left, right, top, bottom int) {
	x1 := left - 20
	y1 := top - 20
	x2 := right + 20
	y2 := bottom + 20

	if x1 < 0 {
		x1 = 0
	}
	if y1 < 0 {
		y1 = 0
	}
	if x2 > FrameWidth {
		x2 = FrameWidth
	}
	if y2 > FrameHeight {
		y2 = FrameHeight
	}

	faceWidth := x2 - x1
	faceHeight := y2 - y1

	if faceWidth <= 0 || faceHeight <= 0 {
		return
	}

	timestamp := time.Now().UnixMilli()
	tempFile := fmt.Sprintf("%s/temp_%d.rgb", OutputDir, timestamp)
	outputFile := fmt.Sprintf("%s/face_%d.bmp", OutputDir, timestamp)

	if err := os.WriteFile(tempFile, rgbData, 0644); err != nil {
		fmt.Printf("Failed to save temp file: %v\n", err)
		return
	}

	cmd := exec.Command(
		"ffmpeg",
		"-f", "rawvideo",
		"-pix_fmt", "rgb24",
		"-s", fmt.Sprintf("%dx%d", FrameWidth, FrameHeight),
		"-i", tempFile,
		"-vf", fmt.Sprintf("crop=%d:%d:%d:%d", faceWidth, faceHeight, x1, y1),
		"-f", "image2",
		"-y",
		outputFile,
	)

	if err := cmd.Run(); err != nil {
		fmt.Printf("ffmpeg crop failed: %v\n", err)
		os.Remove(tempFile)
		return
	}

	os.Remove(tempFile)

	saveCount++
	fmt.Printf("[%d] Saved: %s (face at %d,%d-%d,%d)\n", saveCount, outputFile, x1, y1, x2, y2)
}

func main() {
	if len(os.Args) < 3 {
		fmt.Println("Usage: face_capture_go <rtmp_url> <model_path>")
		fmt.Println("Example: face_capture_go rtmp://192.168.1.62/live/qingting /home/cat/memoark/gst-rknn-filter/model/RetinaFace_mobile320.rknn")
		os.Exit(1)
	}

	rtmpURL := os.Args[1]
	modelPath := os.Args[2]

	app := newFaceCropApp(rtmpURL, modelPath)
	if err := app.run(); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}
