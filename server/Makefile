# Copyright Vladislav Aleinik, 2025

#-------
# Files
#-------

ifeq ($(PROGRAM)$(LIBRARY),)
error:
	@printf "$(BRED)Specify build target!\n"
endif

ifneq ($(PROGRAM),)
EXECUTABLE = build/$(PROGRAM)
endif
ifneq ($(LIBRARY),)
LIB = libs/$(LIBRARY)
endif
# By default, build executable:
# NOTE: first target in the file is the default.

default: $(LIB) $(EXECUTABLE)

#-----------------------
# Compiler/linker flags
#-----------------------

CC := gcc


# Compiler flags:
CFLAGS = \
	-std=c2x \
	-Wall    \
	-Wextra  \
	-Werror

# Linker flags:
LDFLAGS = -pthread -lrt -lm
CLIBFLAGS = -fpic -shared 
# Select build mode:
# NOTE: invoke with "DEBUG=1 make" or "make DEBUG=1".
ifeq ($(DEBUG),1)
	# Add default symbols:
	CFLAGS += -g
else
	# Enable link-time optimization:
	CFLAGS  += -flto
	LDFLAGS += -flto
endif

#--------
# Colors
#--------

# Use ANSI color codes:
BRED    = \033[1;31m
BGREEN  = \033[1;32m
BYELLOW = \033[1;33m
GREEN   = \033[1;35m
BCYAN   = \033[1;36m
RESET   = \033[0m

#-------------------
# Build/run process
#-------------------
libs/%: %.c
	@printf "$(BYELLOW)Building library $(BCYAN)$<$(RESET)\n"
	@mkdir -p libs
	$(CC) $(CLIBFLAGS) $< -o libs/lib$(LIBRARY).so $(LDFLAGS)
	@sudo cp libs/lib$(LIBRARY).so /usr/local/lib
	@sudo ldconfig
	@printf "$(BYELLOW)Library $(BCYAN)lib$(LIBRARY)$(BYELLOW) installed to /usr/local/lib$(RESET)\n"


build/%: %.c
	@printf "$(BYELLOW)Building program $(BCYAN)$<$(RESET)\n"
	@mkdir -p build
	$(CC) $< $(CFLAGS) -o $@ $(LDFLAGS) -l$(LIBRARY)
	@printf "$(BYELLOW)Program $(BCYAN)$<$(BYELLOW) built to $(BCYAN)$@$(RESET)\n"

run: $(EXECUTABLE) $(DUMMY_SRC)
	@./$(EXECUTABLE) $(DUMMY_SRC) 3

# Timing command usage:
TIME_CMD    = /usr/bin/time
TIME_FORMAT = \
	"CPU Percentage: %P\nReal time: %e sec\nUser time: %U sec"

time: $(EXECUTABLE) $(DUMMY_SRC)
	@$(TIME_CMD) --quiet --format=$(TIME_FORMAT) $(EXECUTABLE) $(DUMMY_SRC) 3 | cat

#---------------
# Miscellaneous
#---------------

clean:
	@printf "$(BYELLOW)Cleaning build directory$(RESET)\n"
	@rm -rf build

# List of non-file targets:
.PHONY: run clean default
