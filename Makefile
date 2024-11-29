CC = gcc
LD = $(CC)
CFLAGS += -Wall -O3 -std=c11
LDFLAGS += -lXNVCtrl -lX11 -lvulkan -lSDL2  -lm

TARGETS = vk-gsync-demo

.PHONY: default
default: $(TARGETS)

.PHONY: clean
clean:
	-rm -rf *.o core.* *~ $(TARGETS)

vk-gsync-demo: main.o gsync.o vsync.o vulkan.o
	$(LD) $^ $(LDFLAGS) -o $@

main.o: main.c gsync.h vsync.h
gsync.o: gsync.c gsync.h
vsync.o: vsync.c vsync.h
vulkan.o: vulkan.c vulkan.h
