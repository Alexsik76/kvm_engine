package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

const (
	KeyboardDevice = "/dev/hidg0"
	MouseDevice    = "/dev/hidg1"
	
	// KeepAlive interval
	PingInterval = 30 * time.Second
	PongWait     = 60 * time.Second
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

type KeyboardEvent struct {
	Modifiers byte   `json:"modifiers"`
	// Go's encoding/json automatically decodes Base64 strings into []byte
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

func (h *HIDManager) reopenKeyboard() error {
	if h.kbFile != nil {
		h.kbFile.Close()
	}
	kb, err := os.OpenFile(KeyboardDevice, os.O_WRONLY, 0666)
	if err != nil {
		return err
	}
	h.kbFile = kb
	return nil
}

func (h *HIDManager) reopenMouse() error {
	if h.mFile != nil {
		h.mFile.Close()
	}
	m, err := os.OpenFile(MouseDevice, os.O_WRONLY, 0666)
	if err != nil {
		return err
	}
	h.mFile = m
	return nil
}

func (h *HIDManager) SendKeyReport(event KeyboardEvent) error {
	h.kbMu.Lock()
	defer h.kbMu.Unlock()

	// Keyboard report must always be exactly 8 bytes long for standard boot protocol
	report := make([]byte, 8)
	report[0] = event.Modifiers
	// Byte 1 is reserved (usually 0)
	
	// Bytes 2-7 are the pressed keycodes (up to 6)
	// Because frontend sends Base64 which Unmarshals to event.Keys natively, 
	// event.Keys is already the raw bytes (e.g., [4] for 'A')
	for i := 0; i < len(event.Keys) && i < 6; i++ {
		report[i+2] = event.Keys[i]
	}

	_, err := h.kbFile.Write(report)
	if err != nil {
		log.Printf("Keyboard write error: %v. Attempting to reopen device...", err)
		if reopenErr := h.reopenKeyboard(); reopenErr == nil {
			_, err = h.kbFile.Write(report)
			if err != nil {
				log.Printf("Keyboard write retry failed: %v", err)
			} else {
				log.Println("Successfully reopened keyboard device and sent report.")
			}
		} else {
			log.Printf("Failed to reopen keyboard device: %v", reopenErr)
		}
	}
	return err
}

func (h *HIDManager) SendMouseReport(event MouseEvent) error {
	h.mouseMu.Lock()
	defer h.mouseMu.Unlock()

	// Mouse report length can be 4 or 5 depending on descriptors. 
	// Yours takes 4: Buttons, X, Y, Wheel.
	report := []byte{event.Buttons, byte(event.X), byte(event.Y), byte(event.Wheel)}

	_, err := h.mFile.Write(report)
	if err != nil {
		log.Printf("Mouse write error: %v. Attempting to reopen device...", err)
		if reopenErr := h.reopenMouse(); reopenErr == nil {
			_, err = h.mFile.Write(report)
			if err != nil {
				log.Printf("Mouse write retry failed: %v", err)
			} else {
				log.Println("Successfully reopened mouse device and sent report.")
			}
		} else {
			log.Printf("Failed to reopen mouse device: %v", reopenErr)
		}
	}
	return err
}

func (h *HIDManager) ClearAll() {
	// Release all keys and mouse buttons just in case
	h.SendKeyReport(KeyboardEvent{Modifiers: 0, Keys: []byte{}})
	h.SendMouseReport(MouseEvent{Buttons: 0, X: 0, Y: 0, Wheel: 0})
}

func (h *HIDManager) Close() {
	h.ClearAll()
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
	
	log.Println("New control session established from", r.RemoteAddr)

	// Set up ping/pong handler so we detect dead connections when user switches tabs without closing
	conn.SetReadDeadline(time.Now().Add(PongWait))
	conn.SetPongHandler(func(string) error {
		conn.SetReadDeadline(time.Now().Add(PongWait))
		return nil
	})

	// Start a goroutine to send periodic pings
	doneCh := make(chan struct{})
	go func() {
		ticker := time.NewTicker(PingInterval)
		defer ticker.Stop()
		for {
			select {
			case <-ticker.C:
				if err := conn.WriteControl(websocket.PingMessage, []byte{}, time.Now().Add(10*time.Second)); err != nil {
					log.Printf("Ping failed, closing connection: %v", err)
					conn.Close()
					return
				}
			case <-doneCh:
				return
			}
		}
	}()

	// Cleanup on exit
	defer func() {
		close(doneCh)
		conn.Close()
		log.Println("Connection closed, clearing HID state for:", r.RemoteAddr)
		// Crucial: Release all keys so they don't get stuck!
		w.hid.ClearAll()
	}()

	for {
		_, message, err := conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				log.Printf("Unexpected read error: %v", err)
			} else {
				log.Printf("Connection disconnected normally: %v", err)
			}
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

		// Fast path if it's already a map
		// But re-marshaling is safe too
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
				// Don't log normal keypresses to avoid CPU spam
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
