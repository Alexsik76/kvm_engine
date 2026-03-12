package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"sync"

	"github.com/gorilla/websocket"
)

type Config struct {
	Server struct {
		Port           uint16 `json:"port"`
		KeyboardDevice string `json:"keyboard_device"`
		MouseDevice    string `json:"mouse_device"`
	} `json:"server"`
}

var config Config

func getConfigPath() string {
	// Check environment variable first
	if envPath := os.Getenv("KVM_CONFIG"); envPath != "" {
		return envPath
	}

	// Try executable directory first
	if exePath, err := os.Executable(); err == nil {
		configPath := filepath.Join(filepath.Dir(exePath), "config", "config.json")
		if _, err := os.Stat(configPath); err == nil {
			return configPath
		}
	}

	// Fallback to current working directory
	return "./config/config.json"
}

func loadConfig(path string) error {
	if path == "" {
		path = getConfigPath()
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	return json.Unmarshal(data, &config)
}

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

type KeyboardEvent struct {
	Modifiers byte `json:"modifiers"`
	// Go's encoding/json automatically decodes Base64 strings into []byte
	Keys []byte `json:"keys"`
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
	kb, err := os.OpenFile(config.Server.KeyboardDevice, os.O_WRONLY|02000, 0666)
	if err != nil {
		return nil, fmt.Errorf("failed to open keyboard: %v", err)
	}

	m, err := os.OpenFile(config.Server.MouseDevice, os.O_WRONLY|02000, 0666)
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
		log.Printf("Keyboard write error: %v (Host PC may be sleeping, unplugged or ignoring USB)", err)
	}
	return err
}

func (h *HIDManager) SendMouseReport(event MouseEvent) error {
	h.mouseMu.Lock()
	defer h.mouseMu.Unlock()

	report := []byte{event.Buttons, byte(event.X), byte(event.Y), byte(event.Wheel)}

	_, err := h.mFile.Write(report)
	if err != nil {
		log.Printf("Mouse write error: %v (Host PC may be sleeping, unplugged or ignoring USB)", err)
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

	// Cleanup on exit
	defer func() {
		conn.Close()
		log.Println("Connection closed, clearing HID state for:", r.RemoteAddr)
		// Crucial: Release all keys so they don't get stuck!
		w.hid.ClearAll()
	}()

	for {
		_, message, err := conn.ReadMessage()
		if err != nil {
			log.Printf("Read error or connection closed: %v", err)
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
	if err := loadConfig(""); err != nil {
		log.Fatalf("Failed to load config: %v", err)
	}

	hid, err := NewHIDManager()
	if err != nil {
		log.Fatalf("Critical HID failure: %v", err)
	}
	defer hid.Close()

	handler := &WSHandler{hid: hid}

	http.Handle("/ws/control", handler)

	port := fmt.Sprintf(":%d", config.Server.Port)
	log.Printf("HID Server started on %s", port)
	if err := http.ListenAndServe(port, nil); err != nil {
		log.Fatal(err)
	}
}
