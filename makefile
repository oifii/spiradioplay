include ..\makefile.in

TARGET = spiradioplay.exe
FLAGS += -mwindows

all: $(TARGET)

clean:
	$(RM) $(OUTDIR)\$(TARGET)
