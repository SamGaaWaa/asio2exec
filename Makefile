CC := "D:/msys64/ucrt64/bin/g++.exe"
LIB_DIR := D:/msys64/ucrt64/lib

LIB_FILES := $(wildcard $(LIB_DIR)/*.a)

build/asio2exec: main.cpp asio2exec.hpp
	$(CC) main.cpp $(LIB_FILES) \
		-std=c++23 \
		-fconcepts-diagnostics-depth=4 \
		-Wnon-template-friend \
		-Iasio/asio/include \
		-Istdexec-main/include \
		-D_WIN32_WINNT=0x0A00 \
		-L"D:/msys64/ucrt64/lib" \
		"D:/msys64/ucrt64/lib/libws2_32.a" \
		-o build/asio2exec

clean:
	rm build/* -rf