/**
 * @file      TinyGsmHttpsEC200Uxx.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   LGPL-3.0
 * @date      2023-11-29
 *
 */
#pragma once

#include "TinyGsmCommon.h"

enum HttpContentType {
  CONTENT_TYPE_URLENCODED   = 0,  // application/x-www-form-urlencoded
  CONTENT_TYPE_TEXT         = 1,  // text/plain
  CONTENT_TYPE_OCTET_STREAM = 2,  // application/octet-stream
  CONTENT_TYPE_MULTIPART    = 3,  // multipart/form-data
  CONTENT_TYPE_JSON         = 4,  // application/json
  CONTENT_TYPE_JPEG         = 5   // image/jpeg
};


template <class modemType>
class TinyGsmHttpsEC200U {
 private:
  size_t _length;
  String _headers;
  int _timeout;

 public:
  /*
   * Basic functions
   */
  bool https_begin() {
    _length = 0;
    https_end();  // Ensure any previous session is closed

    // Activate PDP Context
    thisModem().sendAT("+QIACT=1");
    if (thisModem().waitResponse(10000UL) != 1) { return false; }

    // Configure HTTP settings for EC200U
    thisModem().sendAT("+QHTTPCFG=\"contextid\",1");
    if (thisModem().waitResponse(3000) != 1) { return false; }

    thisModem().sendAT("+QHTTPCFG=\"responseheader\",1");
    if (thisModem().waitResponse(3000) != 1) { return false; }

    // Enable SSL if using HTTPS
    // thisModem().sendAT("+QHTTPCFG=\"sslctxid\",0");
    // if (thisModem().waitResponse(3000) != 1) { return false; }

    return true;
  }

  void https_end() {
    // Deactivate PDP Context
    _length = 0;
    thisModem().sendAT("+QIDEACT=1");
    thisModem().waitResponse();
  }

  bool https_set_url(const String& url) {
    // Step 1: Notify the module of the URL length
    thisModem().sendAT("+QHTTPURL=", url.length(), ",60");  // Timeout = 60s
    if (thisModem().waitResponse(3000) != 1) { return false; }

    // Step 2: Send the actual URL as raw data
    thisModem().stream.write(url.c_str(), url.length());
    thisModem().stream.write((uint8_t)0x1A);  // Send CTRL+Z (0x1A)

    // Step 3: Wait for the module to process the URL input
    return thisModem().waitResponse() == 1;
  }

  void https_set_timeout(int timeout) {
    // Set the timeout for the HTTP request
    _timeout = timeout;
  }

  bool https_set_content_type(HttpContentType type) {
    // Send the mapped content type
    thisModem().sendAT("+QHTTPCFG=\"contenttype\",", static_cast<int>(type));
    return thisModem().waitResponse(3000) == 1;
  }

  bool https_set_ssl_index(uint8_t sslId) {
    // Set SSL context ID
    thisModem().sendAT("+QHTTPCFG=\"sslctxid\",", sslId);
    return thisModem().waitResponse(3000) == 1;
  }

  bool https_add_header(const char* name, const char* value) {
    String header = String(name) + ": " + String(value);
    thisModem().sendAT("+QHTTPCFG=\"header\",\"", header, "\"");
    return thisModem().waitResponse(3000) == 1;
  }

  bool https_set_break() {
    _length = 0;
    // Cancel any ongoing HTTP request
    thisModem().sendAT("+QHTTPSTOP");
    return thisModem().waitResponse(5000) == 1;
  }

  int https_get(size_t* bodyLength = NULL) {
    // Send GET request with a default timeout of 60 seconds
    thisModem().sendAT("+QHTTPGET=60");
    if (thisModem().waitResponse(3000) != 1) { return -1; }

    // Wait for the response URC
    if (thisModem().waitResponse(60000UL, "+QHTTPGET: ") == 1) {
      int    result = thisModem().streamGetIntBefore(',');  // Should be 0 if successful
      int    status = thisModem().streamGetIntBefore(',');  // HTTP status code
      size_t length = thisModem().streamGetLongLongBefore('\r');  // Response length

      DBG("Result:");
      DBG(result);
      DBG("HTTP Status:");
      DBG(status);
      DBG("Content Length:");
      DBG(length);

      if (bodyLength) { *bodyLength = length; }
      _length = length;
      return status;  // Return the HTTP status code
    }
    return -1;
  }

  String https_header() {
    return _header;
  }

  int https_body(uint8_t* buffer, int buffer_size) {
    if (!buffer || buffer_size <= 0) { return 0; }

    size_t total = https_get_size();
    if (total == 0) { return 0; }

    if (total > buffer_size) {
      total = buffer_size;  // Limit to available buffer size
    }

    uint8_t* tempBuffer = (uint8_t*)TINY_GSM_MALLOC(total + 1);
    if (!tempBuffer) {
      return 0;  // Memory allocation failed
    }

    // Read full response (headers + body)
    thisModem().sendAT("+QHTTPREAD=0,", total);
    if (thisModem().waitResponse(3000) != 1) {
      free(tempBuffer);
      return 0;
    }

    if (thisModem().waitResponse(30000UL, "+QHTTPREAD: ") != 1) {
      free(tempBuffer);
      return 0;
    }

    int length = thisModem().streamGetIntBefore('\n');  // Get response length
    if (thisModem().stream.readBytes(tempBuffer, length) != length) {
      free(tempBuffer);
      return 0;
    }

    tempBuffer[length] = '\0';

    // Find the separator between headers and body (`\r\n\r\n`)
    char* headerEnd = strstr((char*)tempBuffer, "\r\n\r\n");
    if (!headerEnd) {
      free(tempBuffer);
      return 0;  // No valid response
    }

    // Move pointer to start of body
    int headerOffset = (headerEnd + 4) - (char*)tempBuffer;
    int bodyLength   = length - headerOffset;
    if (bodyLength <= 0) {
      free(tempBuffer);
      return 0;  // No body available
    }

    // Copy body data to the provided buffer
    int copyLength = (bodyLength > buffer_size) ? buffer_size : bodyLength;
    memcpy(buffer, tempBuffer + headerOffset, copyLength);
    free(tempBuffer);

    return copyLength;  // Return the number of bytes copied
  }

  size_t https_get_size() {
    return _length;
  }

  int https_post(uint8_t* payload, size_t size, uint32_t inputTimeout = 10000) {
    if (!payload || size == 0) {
      return -1;  // Invalid payload
    }

    // Step 1: Notify the module of the data size and timeout settings
    thisModem().sendAT("+QHTTPPOST=", size, ",60,", inputTimeout);
    if (thisModem().waitResponse(30000UL, "CONNECT") != 1) {
      return -1;  // Failed to enter data mode
    }

    // Step 2: Send the actual payload
    thisModem().stream.write(payload, size);
    thisModem().stream.write((uint8_t)0x1A);  // Send CTRL+Z (0x1A) to finish

    // Step 3: Wait for POST response
    if (thisModem().waitResponse(60000UL, "+QHTTPPOST:") == 1) {
      int result = thisModem().streamGetIntBefore(',');
      int status = thisModem().streamGetIntBefore(',');
      int length = thisModem().streamGetIntBefore('\r');

      DBG("Result:");
      DBG(result);
      DBG("HTTP Status:");
      DBG(status);
      DBG("Content Length:");
      DBG(length);

      return status;  // Return HTTP status code
    }

    return -1;
  }

  int https_post(const String& payload) {
    return https_post((uint8_t*)payload.c_str(), payload.length());
  }

  /**
   * @brief  POSTFile
   * @note   Send file to server
   * @param  *filepath:File path and file name, full path required. C:/file for local
   * storage and D:/file for SD card
   * @param  method:  0 = GET 1 = POST 2 = HEAD 3 = DELETE
   * @param  sendFileAsBody: 0 = Send file as HTTP header and body , 1 = Send file as Body
   * @retval httpCode, -1 = failed
   */
  int https_post_file(const char* filepath, uint8_t method = 1,
                      bool sendFileAsBody = true) {
    if (!filepath || strlen(filepath) < 4) {
      return -1;  // Invalid file path
    }

    // Determine storage type: Internal (`1`) or SD Card (`2`)
    uint8_t storage = (filepath[0] == 'd' || filepath[0] == 'D') ? 2 : 1;

    // Send command to post file
    thisModem().sendAT("+QHTTPPOSTFILE=\"", filepath, "\",", storage, ",", method, ",",
                       sendFileAsBody);
    if (thisModem().waitResponse(120000UL) != 1) {
      return -1;  // File upload failed
    }

    // Wait for HTTP response
    if (thisModem().waitResponse(150000UL, "+QHTTPPOSTFILE:") == 1) {
      int result = thisModem().streamGetIntBefore(',');
      int status = thisModem().streamGetIntBefore(',');
      int length = thisModem().streamGetIntBefore('\r');

      DBG("Result:");
      DBG(result);
      DBG("HTTP Status:");
      DBG(status);
      DBG("Content Length:");
      DBG(length);

      return status;  // Return HTTP status code
    }

    return -1;
  }

  /*
   * CRTP Helper
   */
 protected:
  inline const modemType& thisModem() const {
    return static_cast<const modemType&>(*this);
  }
  inline modemType& thisModem() {
    return static_cast<modemType&>(*this);
  }
};
