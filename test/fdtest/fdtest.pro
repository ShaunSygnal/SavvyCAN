# CAN-FD regression harness for the SavvyCAN tool windows.
#
# Builds the real SavvyCAN sources (minus main.cpp) together with fdtest_main.cpp under
# AddressSanitizer/UndefinedBehaviorSanitizer, then opens each tool window with frames of
# 8..64 bytes and checks a few observable results. It also checks multiplexed-signal decoding in
# Overwrite Mode through a standalone CANFrameModel. Exit code = number of failed checks.
#
#   mkdir build-fdtest && cd build-fdtest
#   qmake ../test/fdtest/fdtest.pro && make -j$(nproc)
#   ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 UBSAN_OPTIONS=suppressions=../test/fdtest/ubsan.supp ./fdtest
#
# (ubsan.supp silences benign floating point divisions by zero inside the bundled QCustomPlot.)
# Runs headless (QT_QPA_PLATFORM=offscreen is set by the harness itself) and writes its
# QSettings to ./settings next to the binary so your real SavvyCAN configuration is untouched.

SRC = $$PWD/../..
include($$SRC/SavvyCAN.pro)

SOURCES -= main.cpp
for(f, SOURCES): ABS_SOURCES += $$SRC/$$f
for(f, HEADERS): ABS_HEADERS += $$SRC/$$f
for(f, FORMS): ABS_FORMS += $$SRC/$$f
for(f, RESOURCES): ABS_RESOURCES += $$SRC/$$f
SOURCES = $$ABS_SOURCES $$PWD/fdtest_main.cpp
HEADERS = $$ABS_HEADERS
FORMS = $$ABS_FORMS
RESOURCES = $$ABS_RESOURCES
INCLUDEPATH += $$SRC
TRANSLATIONS =
INSTALLS =
DISTFILES =

TARGET = fdtest
CONFIG -= release
CONFIG += debug sanitizer sanitize_address sanitize_undefined
DEFINES -= QT_NO_DEBUG_OUTPUT
# keep going after the first report so one run lists every problem
QMAKE_CXXFLAGS += -fsanitize-recover=address -fno-omit-frame-pointer
