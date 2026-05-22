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
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"github.com/go-gst/go-gst/gst"
	gstapp "github.com/go-gst/go-gst/gst/app"
)

const (
	OutputDir = "/tmp/faces_go_jpeg"
)

type FaceResult struct {
	Name   string
	Left   int
	Right  int
	Top    int
	Bottom int
	Prop   float32
}

type CachedFaceMeta struct {
	FaceCount int
	Faces     []FaceResult
	PTS       int64
	Valid     bool
}

type FaceCaptureApp struct {
	rtmpURL   string
	modelPath string
	pipeline  *gst.Pipeline
	metaSink  *gstapp.Sink
	jpegSink  *gstapp.Sink

	cachedMeta CachedFaceMeta
	metaMutex  sync.Mutex
	frameCount int
	saveCount  int
}

func main() {
	gst.Init(nil)

	os.MkdirAll(OutputDir, 0755)

	app := &FaceCaptureApp{
		rtmpURL:   "rtmp://192.168.1.62/live/qingting",
		modelPath: "/home/cat/memoark/gst-rknn-filter/model/RetinaFace_mobile320.rknn",
	}

	if len(os.Args) >= 3 {
		app.rtmpURL = os.Args[1]
		app.modelPath = os.Args[2]
	}

	fmt.Println("Testing tee + dual appsink mode (meta + JPEG) in Go")
	fmt.Printf("RTMP URL: %s\n", app.rtmpURL)
	fmt.Printf("Model: %s\n", app.modelPath)
	fmt.Printf("Output directory: %s\n", OutputDir)

	if err := app.run(); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}

func (app *FaceCaptureApp) run() error {
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
			"tee name=t "+
			"t. ! queue ! appsink name=meta_sink sync=false "+
			"t. ! queue ! videoconvert ! video/x-raw,format=NV12 ! mppjpegenc ! appsink name=jpeg_sink sync=false",
		app.rtmpURL, app.modelPath)

	fmt.Printf("Pipeline: %s\n", pipelineStr)

	pipeline, err := gst.NewPipelineFromString(pipelineStr)
	if err != nil {
		return fmt.Errorf("failed to create pipeline: %w", err)
	}
	app.pipeline = pipeline

	metaElem, err := pipeline.GetElementByName("meta_sink")
	if err != nil {
		return fmt.Errorf("failed to get meta_sink: %w", err)
	}
	app.metaSink = gstapp.SinkFromElement(metaElem)

	jpegElem, err := pipeline.GetElementByName("jpeg_sink")
	if err != nil {
		return fmt.Errorf("failed to get jpeg_sink: %w", err)
	}
	app.jpegSink = gstapp.SinkFromElement(jpegElem)

	app.metaSink.SetCallbacks(&gstapp.SinkCallbacks{
		NewSampleFunc: func(sink *gstapp.Sink) gst.FlowReturn {
			return app.onMetaSample(sink)
		},
	})

	app.jpegSink.SetCallbacks(&gstapp.SinkCallbacks{
		NewSampleFunc: func(sink *gstapp.Sink) gst.FlowReturn {
			return app.onJpegSample(sink)
		},
	})

	if err := pipeline.SetState(gst.StatePlaying); err != nil {
		return fmt.Errorf("failed to start pipeline: %w", err)
	}

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		<-sigChan
		fmt.Println("\nStopping...")
		app.pipeline.SendEvent(gst.NewEOSEvent())
	}()

	bus := pipeline.GetPipelineBus()
	for {
		msg := bus.TimedPopFiltered(gst.ClockTimeNone, gst.MessageEOS|gst.MessageError)
		if msg == nil {
			continue
		}

		switch msg.Type() {
		case gst.MessageEOS:
			fmt.Println("EOS received")
			goto done
		case gst.MessageError:
			gerr := msg.ParseError()
			fmt.Fprintf(os.Stderr, "Error: %v\n", gerr)
			goto done
		}
	}

done:
	pipeline.SetState(gst.StateNull)
	return nil
}

func (app *FaceCaptureApp) onMetaSample(sink *gstapp.Sink) gst.FlowReturn {
	sample := sink.PullSample()
	if sample == nil {
		return gst.FlowOK
	}
	defer sample.Unref()

	buffer := sample.GetBuffer()
	if buffer == nil {
		return gst.FlowOK
	}

	faceMeta := app.getFaceMeta(buffer)
	if faceMeta != nil && faceMeta.FaceCount > 0 {
		app.metaMutex.Lock()
		app.cachedMeta.FaceCount = faceMeta.FaceCount
		app.cachedMeta.Faces = faceMeta.Faces
		app.cachedMeta.PTS = int64(buffer.PresentationTimestamp())
		app.cachedMeta.Valid = true
		app.frameCount++
		app.metaMutex.Unlock()
	} else {
		app.metaMutex.Lock()
		app.cachedMeta.Valid = false
		app.metaMutex.Unlock()
	}

	return gst.FlowOK
}

func (app *FaceCaptureApp) onJpegSample(sink *gstapp.Sink) gst.FlowReturn {
	sample := sink.PullSample()
	if sample == nil {
		return gst.FlowOK
	}
	defer sample.Unref()

	buffer := sample.GetBuffer()
	if buffer == nil {
		return gst.FlowOK
	}

	caps := sample.GetCaps()
	if caps == nil {
		return gst.FlowOK
	}

	structure := caps.GetStructureAt(0)
	widthVal, _ := structure.GetValue("width")
	heightVal, _ := structure.GetValue("height")
	width := widthVal.(int)
	height := heightVal.(int)

	app.metaMutex.Lock()
	defer app.metaMutex.Unlock()

	if !app.cachedMeta.Valid || app.cachedMeta.FaceCount == 0 {
		return gst.FlowOK
	}

	centerX := width / 2
	centerY := height / 2
	var nearest *FaceResult
	minDist := float32(1e9)

	for i := 0; i < app.cachedMeta.FaceCount; i++ {
		face := &app.cachedMeta.Faces[i]
		faceCenterX := (face.Left + face.Right) / 2
		faceCenterY := (face.Top + face.Bottom) / 2
		dx := float32(faceCenterX - centerX)
		dy := float32(faceCenterY - centerY)
		dist := dx*dx + dy*dy

		if dist < minDist {
			minDist = dist
			nearest = face
		}
	}

	if nearest != nil {
		x1 := max(0, nearest.Left-20)
		y1 := max(0, nearest.Top-20)
		x2 := min(width, nearest.Right+20)
		y2 := min(height, nearest.Bottom+20)

		faceWidth := x2 - x1
		faceHeight := y2 - y1

		if faceWidth > 0 && faceHeight > 0 {
			mapInfo := buffer.Map(gst.MapRead)
			if mapInfo != nil {
				data := C.GoBytes(mapInfo.Data(), C.int(mapInfo.Size()))
				buffer.Unmap()

				timestamp := time.Now().Unix()
				tempJpeg := fmt.Sprintf("%s/temp_%d.jpg", OutputDir, timestamp)
				outputFile := fmt.Sprintf("%s/face_%d.jpg", OutputDir, timestamp)

				if err := os.WriteFile(tempJpeg, data, 0644); err == nil {
					ffmpegCmd := exec.Command("ffmpeg",
						"-i", tempJpeg,
						"-vf", fmt.Sprintf("crop=%d:%d:%d:%d", faceWidth, faceHeight, x1, y1),
						"-y", outputFile)

					var stderr bytes.Buffer
					ffmpegCmd.Stderr = &stderr

					if err := ffmpegCmd.Run(); err != nil {
						fmt.Printf("[JPEG] ffmpeg error: %v, stderr: %s\n", err, stderr.String())
					} else {
						app.saveCount++
						fmt.Printf("[JPEG] [%d] Saved: %s (face at %d,%d-%d,%d)\n",
							app.saveCount, outputFile, x1, y1, x2, y2)
					}

					os.Remove(tempJpeg)
				} else {
					fmt.Printf("[JPEG] Failed to write temp file: %v\n", err)
				}
			}
		}
	}

	app.cachedMeta.Valid = false

	return gst.FlowOK
}

func (app *FaceCaptureApp) getFaceMeta(buffer *gst.Buffer) *CachedFaceMeta {
	nativeBuffer := buffer.Instance()
	var cResult C.FaceMetaResult
	ret := C.gst_face_meta_extract((C.gpointer)(nativeBuffer), &cResult)

	if ret <= 0 {
		return nil
	}

	result := &CachedFaceMeta{
		FaceCount: int(cResult.face_count),
		Faces:     make([]FaceResult, cResult.face_count),
		Valid:     true,
	}

	for i := 0; i < int(cResult.face_count); i++ {
		result.Faces[i] = FaceResult{
			Name:   C.GoString(&cResult.names[i][0]),
			Left:   int(cResult.lefts[i]),
			Right:  int(cResult.rights[i]),
			Top:    int(cResult.tops[i]),
			Bottom: int(cResult.bottoms[i]),
			Prop:   float32(cResult.props[i]),
		}
	}

	return result
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}
