// Fix: ESP32-S3 Arduino defines FSPI=0, but the SDK REG_SPI_BASE(0)=0 (invalid).
// TFT_eSPI has a proper REG_SPI_BASE guarded by #ifndef. Since Arduino.h → soc.h
// defines REG_SPI_BASE first, TFT_eSPI's fix is skipped and we crash.
// Solution: include Arduino.h here, then #undef REG_SPI_BASE so TFT_eSPI can fix it.
#include <Arduino.h>
#ifdef REG_SPI_BASE
#undef REG_SPI_BASE
#endif
