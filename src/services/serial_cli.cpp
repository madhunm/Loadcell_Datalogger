    #include "services/serial_cli.h"

    #include <Arduino.h>
    #include <cstring>

    #include "services/aux_state.h"
    #include "services/sd_logger.h"
    #include "services/scope_stream.h"

    static char linebuf[96];
    static size_t linelen = 0;

    static void print_help() {
      Serial.println("# Commands:");
      Serial.println("#  status");
      Serial.println("#  scope <hz>   (hz must divide 500; 10/20/25/50 recommended)");
      Serial.println("#  scope 0      (stop)");
      Serial.println("#  startlog");
      Serial.println("#  stoplog");
    }

    static void cli_task(void*) {
      Serial.println("#CLI ready.");
      print_help();

      bool header_sent = false;

      while (true) {
        while (Serial.available()) {
          char c = (char)Serial.read();
          if (c == '\r') continue;
          if (c == '\n') {
            linebuf[linelen] = 0;

            if (strcmp(linebuf, "help") == 0) {
              print_help();
            } else if (strcmp(linebuf, "status") == 0) {
              Serial.printf("#status: logging=%d\n", (int)logger_is_logging());
            } else if (strncmp(linebuf, "scope", 5) == 0) {
              int hz = 0;
              sscanf(linebuf, "scope %d", &hz);
              if (hz == 0) {
                scope_set_rate(0);
                Serial.println("#scope stopped");
              } else if (500 % hz != 0) {
                Serial.println("#ERR: scope_hz must divide 500");
              } else {
                scope_set_rate((uint16_t)hz);
                header_sent = false;
                Serial.printf("#scope started %d Hz\n", hz);
              }
            } else if (strcmp(linebuf, "startlog") == 0) {
              AuxSnapshot snap = aux_get_snapshot();
              auto hdr = logger_make_default_header();
              if (logger_start_session(hdr, snap.rtc_valid, snap.rtc_epoch)) Serial.println("#log started");
              else Serial.println("#ERR: log start failed (logger busy or finalizing)");
            } else if (strcmp(linebuf, "stoplog") == 0) {
              logger_stop_session();
            } else if (linelen != 0) {
              Serial.println("#ERR: unknown cmd (type help)");
            }

            linelen = 0;
          } else if (linelen < sizeof(linebuf) - 1) {
            linebuf[linelen++] = c;
          }
        }

        if (scope_is_enabled()) {
          if (!header_sent) {
            Serial.println("#fields: ms,force_mean_N,force_peak_N,accel_mag_g,flags");
            header_sent = true;
          }

          ScopeSample s;
          if (g_scope_q && xQueueReceive(g_scope_q, &s, 0) == pdTRUE) {
            Serial.printf("%u,%.3f,%.3f,%.3f,0x%04X\n",
                          s.ms, s.force_mean_N, s.force_peak_N, s.accel_mag_g, s.flags);
          }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
      }
    }

    void start_cli_task() {
      xTaskCreatePinnedToCore(cli_task, "cli", 4096, nullptr, 2, nullptr, 0);
    }
