
CC=c++
ARGS=-std=c++17 -Wall -Wextra -pedantic
OBJS=main.o copy.o names.o
BIN=filesync

$(BIN): build_date $(OBJS) Makefile
	$(CC) $(OBJS) -o $(BIN)

main.o: main.cc globals.h build_date.h
	$(CC) $(ARGS) -c main.cc

copy.o: copy.cc globals.h
	$(CC) $(ARGS) -c copy.cc

names.o: names.cc globals.h
	$(CC) $(ARGS) -c names.cc

build_date:
	echo "#define BUILD_DATE \"`date -u +'%F %T %Z'`\"" > build_date.h

clean:
	rm -f $(BIN) $(OBJS) core* build_date.h 
