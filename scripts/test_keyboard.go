package main

import (
	"log"
	"net/url"
	"time"

	"github.com/gorilla/websocket"
)

// HID Command structures
type KeyboardData struct {
	Modifiers byte   `json:"modifiers"`
	Keys      []byte `json:"keys"`
}

type Command struct {
	Type string       `json:"type"`
	Data KeyboardData `json:"data"`
}

// Map of some basic characters to HID keycodes
var keyMap = map[rune]byte{
	'a': 4, 'b': 5, 'c': 6, 'd': 7, 'e': 8, 'f': 9, 'g': 10, 'h': 11,
	'i': 12, 'j': 13, 'k': 14, 'l': 15, 'm': 16, 'n': 17, 'o': 18, 'p': 19,
	'q': 20, 'r': 21, 's': 22, 't': 23, 'u': 24, 'v': 25, 'w': 26, 'x': 27,
	'y': 28, 'z': 29, ' ': 44,
}

func main() {
	// 1. Connect to the local HID server
	u := url.URL{Scheme: "ws", Host: "localhost:8080", Path: "/ws/control"}
	log.Printf("Connecting to %s", u.String())

	c, _, err := websocket.DefaultDialer.Dial(u.String(), nil)
	if err != nil {
		log.Fatal("Dial error:", err)
	}
	defer c.Close()

	// 2. Type "hello"
	message := "hello from rpi"
	log.Printf("Typing: %s", message)

	for _, char := range message {
		keycode, ok := keyMap[char]
		if !ok {
			continue
		}

		// Key Down
		press := Command{
			Type: "keyboard",
			Data: KeyboardData{Modifiers: 0, Keys: []byte{keycode}},
		}
		c.WriteJSON(press)
		time.Sleep(20 * time.Millisecond)

		// Key Up (Release)
		release := Command{
			Type: "keyboard",
			Data: KeyboardData{Modifiers: 0, Keys: []byte{0}},
		}
		c.WriteJSON(release)
		time.Sleep(50 * time.Millisecond)
	}

	log.Println("Test sequence complete.")
}