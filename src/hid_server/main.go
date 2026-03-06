package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"sync"

	"github.com/gorilla/websocket"
)

// HID Devices paths
const (
	KeyboardDevice = "/dev/hidg0"
	MouseDevice    = "/dev/hidg1"
)

// Upgrader for WebSocket connections
var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true // Allow all origins for the KVM internal tool
	},
}

// KeyboardEvent represents a standard HID keyboard report structure
type KeyboardEvent struct {
	Modifiers byte   `json:"modifiers"` // Shift, Ctrl, Alt, etc.
	Keys      []byte `json:"keys"`      // Up to 6 keycodes
}

// MouseEvent represents a standard HID mouse report structure
type MouseEvent struct {
	Buttons byte `json:"buttons"` // Left, Right, Middle clicks
	X       int8 `json:"x"`       // Relative X movement (-127 to 127)
	Y       int8 `json:"y"`       // Relative Y movement (-127 to 127)
	Wheel   int8 `json:"wheel"`   // Vertical scroll
}

// HIDManager handles low-level writes to device files
type HIDManager struct {
	kbMu    sync.Mutex
	mouseMu sync.Mutex
	kbFile  *os.File
	mFile   *os.File
}

// NewHIDManager opens device files for writing
func NewHIDManager() (*HIDManager, error) {
	kb, err := os.OpenFile(KeyboardDevice, os.O_WRONLY, 0666)
	if err != nil {
		return nil, fmt.Errorf("failed to open keyboard: %v", err)
	}

	m, err := os.OpenFile(MouseDevice, os.O_WRONLY, 0666)
	if err != nil {
		kb.Close()
		return nil, fmt.Errorf("failed to open mouse: %v", err)
	}

	return &HIDManager{
		kbFile: kb,
		mFile:  m,
	}, nil
}

// SendKeyReport writes an 8-byte keyboard report
func (h *HIDManager) SendKeyReport(event KeyboardEvent) error {
	h.kbMu.Lock()
	defer h.kbMu.Unlock()

	// HID Keyboard report: 8 bytes
	// 0: modifiers, 1: reserved, 2-7: keycodes
	report := make([]byte, 8)
	report[0] = event.Modifiers
	for i := 0; i < len(event.Keys) && i < 6; i++ {
		report[i+2] = event.Keys[i]
	}

	_, err := h.kbFile.Write(report)
	return err
}

// SendMouseReport writes a 4-byte mouse report
func (h *HIDManager) SendMouseReport(event MouseEvent) error {
	h.mouseMu.Lock()
	defer h.mouseMu.Unlock()

	// HID Mouse report: 4 bytes
	// 0: buttons, 1: x, 2: y, 3: wheel
	report := []byte{event.Buttons, byte(event.X), byte(event.Y), byte(event.Wheel)}

	_, err := h.mFile.Write(report)
	return err
}

func (h *HIDManager) Close() {
	h.kbFile.Close()
	h.mFile.Close()
}

// WSHandler manages WebSocket communication
type WSHandler struct {
	hid *HIDManager
}

func (w *WSHandler) ServeHTTP(rw http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(rw, r, nil)
	if err != nil {
		log.Printf("WS Upgrade error: %v", err)
		return
	}
	defer conn.Close()

	log.Println("New control session established")

	for {
		_, message, err := conn.ReadMessage()
		if err != nil {
			log.Printf("Read error: %v", err)
			break
		}

		// Generic message to determine type
		var generic map[string]interface{}
		if err := json.Unmarshal(message, &generic); err != nil {
			continue
		}

		msgType, ok := generic["type"].(string)
		if !ok {
			continue
		}

		switch msgType {
		case "keyboard":
			var kb KeyboardEvent
			if data, err := json.Marshal(generic["data"]); err == nil {
				json.Unmarshal(data, &kb)
				w.hid.SendKeyReport(kb)
			}
		case "mouse":
			var m MouseEvent
			if data, err := json.Marshal(generic["data"]); err == nil {
				json.Unmarshal(data, &m)
				w.hid.SendMouseReport(m)
			}
		}
	}
}

func main() {
	hid, err := NewHIDManager()
	if err != nil {
		log.Fatalf("Critical HID failure: %v", err)
	}
	defer hid.Close()

	handler := &WSHandler{hid: hid}

	http.Handle("/ws/control", handler)

	port := ":8080"
	log.Printf("HID Server started on %s", port)
	if err := http.ListenAndServe(port, nil); err != nil {
		log.Fatal(err)
	}
}