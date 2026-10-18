const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  while (file.available()) {
    int bytesRead = file.read(inputBuffer, inputChunkSize);
    if (bytesRead == 0) break;

    // Direct byte-to-base64 streaming conversion logic
    uint32_t b24 = 0;
    for (int i = 0; i < bytesRead; i++) {
      b24 |= inputBuffer[i] << (16 - (i * 8));
    }

    client.print(b64_table[(b24 >> 18) & 0x3F]);
    client.print(b64_table[(b24 >> 12) & 0x3F]);
    client.print(bytesRead > 1 ? b64_table[(b24 >> 6) & 0x3F] : '=');
    client.print(bytesRead > 2 ? b64_table[b24 & 0x3F] : '=');
  }
  file.close();