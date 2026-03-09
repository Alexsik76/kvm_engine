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

const (
	KeyboardDevice = "/dev/hidg0"
	MouseDevice    = "/dev/hidg1"
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

type KeyboardEvent struct {
	Modifiers byte   `json:"modifiers"`
	Keys      []byte `json:"keys"`
}

type MouseEvent struct {
	Buttons byte `json:"buttons"`
	X       int8 `json:"x"`
	Y       int8 `json:"y"`
	Wheel   int8 `json:"wheel"`
}

type HIDManager struct {
	kbMu    sync.Mutex
	mouseMu sync.Mutex
	kbFile  *os.File
	mFile   *os.File
}

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

func (h *HIDManager) SendKeyReport(event KeyboardEvent) error {
	h.kbMu.Lock()
	defer h.kbMu.Unlock()

	report := make([]byte, 8)
	report[0] = event.Modifiers
	for i := 0; i < len(event.Keys) && i < 6; i++ {
		report[i+2] = event.Keys[i]
	}

	_, err := h.kbFile.Write(report)
	if err != nil {
		log.Printf("Keyboard write error: %v", err)
	}
	return err
}

func (h *HIDManager) SendMouseReport(event MouseEvent) error {
	h.mouseMu.Lock()
	defer h.mouseMu.Unlock()

	report := []byte{event.Buttons, byte(event.X), byte(event.Y), byte(event.Wheel)}

	_, err := h.mFile.Write(report)
	if err != nil {
		log.Printf("Mouse write error: %v", err)
	}
	return err
}

func (h *HIDManager) Close() {
	h.kbFile.Close()
	h.mFile.Close()
}

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

	log.Println("New control session established from", r.RemoteAddr)

	for {
		_, message, err := conn.ReadMessage()
		if err != nil {
			log.Printf("Read error (Connection closed?): %v", err)
			break
		}

		var generic map[string]interface{}
		if err := json.Unmarshal(message, &generic); err != nil {
			log.Printf("JSON Parse error: %v", err)
			continue
		}

		msgType, ok := generic["type"].(string)
		if !ok {
			log.Printf("Error: 'type' field is missing or not a string")
			continue
		}

		dataObj, ok := generic["data"]
		if !ok {
			log.Printf("Error: 'data' field is missing")
			continue
		}

		dataBytes, err := json.Marshal(dataObj)
		if err != nil {
			log.Printf("Error re-marshaling the 'data' object: %v", err)
			continue
		}

		switch msgType {
		case "keyboard":
			var kb KeyboardEvent
			if err := json.Unmarshal(dataBytes, &kb); err != nil {
				log.Printf("Failed to map JSON to KeyboardEvent struct: %v", err)
			} else {
				w.hid.SendKeyReport(kb)
			}
		case "mouse":
			var m MouseEvent
			if err := json.Unmarshal(dataBytes, &m); err != nil {
				log.Printf("Failed to map JSON to MouseEvent struct: %v", err)
			} else {
				w.hid.SendMouseReport(m)
			}
		default:
			log.Printf("Unknown message type received: %s", msgType)
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