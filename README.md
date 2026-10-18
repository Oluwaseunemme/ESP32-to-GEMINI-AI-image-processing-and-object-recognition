# ESP32-Gemini-Vision

A lightweight, production-ready framework to send images from an ESP32’s **LittleFS** filesystem directly to the **Google Gemini Pro Vision API** via secure HTTPS—completely eliminating the need for a PC or Python middleman.

---

## 🚀 The Core Achievement
This project successfully achieves **on-device edge intelligence**. The ESP32 independently streams image assets stored on its internal flash directly into the Gemini REST API (`://googleapis.com`) by encoding and transmitting raw binary data into Base64 format on-the-fly, utilizing only **3 bytes of RAM** overhead for the stream.

---

## 🛠️ The Journey: Overcoming Failures & Roadblocks

Building this project required solving a series of hard hardware constraints, memory limits, and network anomalies. Here is how those roadblocks were defeated:

### 1. The Overloaded Core & Memory Collapses
* **The Problem:** The ESP32 repeatedly encountered fatal `EXCCAUSE: 0x0000001c` crashes (Null Pointer/LoadStoreAlignment errors) on Core 1 when making secure HTTPS requests.
* **The Culprit:** The standard `WiFiClientSecure` stack allocates a massive **32KB buffer** for TLS handshakes, which starves the ESP32’s heap memory. Additionally, higher-level libraries like `HTTPClient` struggled to bridge custom chunked streaming logic gracefully.
* **The Evolution & Fix:** We upgraded the underlying firmware core toolchain to utilize the modern `mbedtls`-backed `NetworkClientSecure` framework. This modern layer dynamically manages allocation limits natively, stabilizing Core 1 execution pools during massive payload deployments.

### 2. Serial Ring Buffer Overflows (90% Drop Glitch)
* **The Problem:** When uploading images from Python to the ESP32 via standard Serial communication, the transfer consistently locked up permanently after receiving ~90% of the asset bytes.
* **The Culprit:** The ESP32's default hardware serial RX buffer is only **256 bytes**. While the ESP32 took a brief execution break to write an incoming 1024-byte chunk to LittleFS, Python continued to dump data at high speed down the line, completely overflowing the hardware queue and dropping the ending bytes.
* **The Fix:** Two adjustments were made:
  1. Expanded the ESP32's hardware RX buffer pool inside `Serial.begin()` to **2048 bytes** to handle write delays.
  2. Implemented a tiny **10ms paced interval sleep** inside the Python client chunk loop (`1024 bytes per burst`) to match flash memory cycles perfectly.

### 3. Stale Bootloader Strings (`b'ets '`)
* **The Problem:** Python received an invalid file size header equivalent to `544437349` instead of a 4-byte little-endian integer.
* **The Culprit:** Python was accidentally reading stale boot ROM logs (`ets Jul 29 2019...`) left over in its input buffer from the ESP32’s boot phase.
* **The Fix:** Implemented `ser.reset_input_buffer()` in Python right before triggering requests to cleanly wipe background strings.

### 4. File Corruption via Truncated Identifiers
* **The Problem:** Gemini threw raw API parsing anomalies and failed to recognize valid image streams.
* **The Culprit:** Files moved to LittleFS over early unbuffered custom serial pipelines dropped their initialization bytes. Because the first few signature bytes of a JPEG/PNG determine its filesystem identifier, the files became unreadable.
* **The Fix:** Swapped to a dedicated **HTTP multipart web server upload engine** on the ESP32, completely sanitizing filename properties using `.trim()` and ensuring robust file validity on flash.

### 5. HTTP Chunked Transfer Encoding Noise
* **The Problem:** The raw API strings returned with random characters like `917` at the beginning and `0` at the end, causing `ArduinoJson` deserialization failures.
* **The Culprit:** Google streams large text responses using *HTTP Chunked Transfer Encoding*. The `917` was simply the hexadecimal marker for chunk sizes (`0x917`).
* **The Fix:** Created an ultra-fast delimiter slice to pull only the valid object schema:
  ```cpp
  int jsonStart = rawResponse.indexOf('{');
  int jsonEnd = rawResponse.lastIndexOf('}');
  String responseText = rawResponse.substring(jsonStart, jsonEnd + 1);
  ```

---

## 🔒 Robust API Resilience (Handling 503 Errors)
To prevent network drops or Google rate-limiting from crashing automated edge operations, the ESP32 uses an exponential-backoff retry matrix. If the server drops an HTTP `503 Service Unavailable` response, the code dynamically expands connection intervals across finite fallback counts to safely clear queue limits before gracefully reporting states.

---

## 🎯 Practical Use Cases & Event Triggering
By processing Gemini’s clean JSON candidates array using `ArduinoJson`, the ESP32 parses unstructured visual text descriptions into immediate hardware interactions.

```text
                  +-------------------+

                  |   ESP32-CAM       |
                  |  (Captures Image) |
                  +---------+---------+
                            |
                            v (Saves to LittleFS)
                  +-------------------+

                  |     LittleFS      |
                  +---------+---------+
                            |
                            v (Streams to Cloud)
               +------------+------------+

               | Google Gemini Vision API|
               +------------+------------+
                            |
                            v (Returns parsed JSON string)
                  +-------------------+

                  |    ArduinoJson    |
                  +---------+---------+
                            |
                            | (Text Match Parsing)
                            v
          +-----------------+-----------------+

          |                                   |
          v                                   v
  +---------------+                   +---------------+

  | GPIO Trigger  |                   | Serial Logging|
  | (Relay/LED)   |                   | (Automation)  |
  +---------------+                   +---------------+
```

* **Industrial Automation:** Querying prompts like `"Is the safety switch up or down?"` allows logic gates to execute digital pin toggles (`digitalWrite(RELAY_PIN, HIGH)`).
* **Security & Verification:** Capturing structural environment details to drive smart home triggers without demanding manual threshold calibration.

---

## 🔮 Future Enhancements
* **Direct ESP-CAM Integration:** Pipeline raw framebuffer bytes directly from `esp_camera_fb_get()` into the Base64 stream to remove the LittleFS intermediate flash write dependency entirely.
* **Streamlined JSON Buffers:** Implement static JSON parsing filters to discard unwanted meta fields early, reducing internal heap usage down to absolute bare minimums.
