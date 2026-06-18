# WebServ Makefile
# Compiles non-blocking HTTP/1.1 server in C++98

# Color definitions
RESET      = \033[0m
RED        = \033[31m
GREEN      = \033[32m
YELLOW     = \033[33m
CYAN       = \033[36m

# Compiler and flags
CXX        = c++
CXXFLAGS   = -Wall -Wextra -Werror -std=c++98
INCLUDES   = -I./includes

# Directories
SRC_DIR    = src
OBJ_DIR    = obj
INCLUDE_DIR = includes

# Source files (recursive find)
SOURCES    = $(shell find $(SRC_DIR) -type f -name "*.cpp")
OBJECTS    = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SOURCES))

# Executable name
TARGET     = webserv

# Default target
all: $(TARGET)

# Link executable
$(TARGET): $(OBJECTS)
	@echo "$(CYAN)🔗 Linking...$(RESET)"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(OBJECTS)
	@echo "$(GREEN)✅ Compilation complete! ./$(TARGET)$(RESET)"

# Compile object files (maintain directory structure)
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "$(YELLOW)🔨 Compiling $<$(RESET)"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# Dash prefix = silent on first build when .d files don't exist yet
-include $(OBJECTS:.o=.d)

# Clean object files
clean:
	@echo "$(RED)🧹 Cleaning object files...$(RESET)"
	@rm -rf $(OBJ_DIR)
	@echo "$(GREEN)✅ Clean complete!$(RESET)"

# Full clean (objects + executable)
fclean: clean
	@echo "$(RED)🗑️  Removing executable...$(RESET)"
	@rm -f $(TARGET)
	@rm -f $(UNIT_BINS)
	@echo "$(GREEN)✅ Full clean complete!$(RESET)"

# Rebuild
re: fclean all

# Unit tests (phase 2.1+). One small two-file build per unit on purpose:
# request.cpp and response.cpp each have no deps, so a test needs no sockets,
# no server, no main.o. Same flags as the real build -- evaluators compile
# everything they find in the repo. Add a unit -> add its pair here.
REQ_SRC    = tests/unit/test_request.cpp src/http/request.cpp
REQ_BIN    = tests/unit/run_request_tests

RESP_SRC   = tests/unit/test_response.cpp src/http/response.cpp
RESP_BIN   = tests/unit/run_response_tests

UNIT_BINS  = $(REQ_BIN) $(RESP_BIN)

unit: $(UNIT_BINS)
	@./$(REQ_BIN)
	@./$(RESP_BIN)

$(REQ_BIN): $(REQ_SRC) $(INCLUDE_DIR)/http.hpp $(INCLUDE_DIR)/string_utils.hpp
	@echo "$(CYAN)🧪 Building request unit tests...$(RESET)"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(REQ_BIN) $(REQ_SRC)

$(RESP_BIN): $(RESP_SRC) $(INCLUDE_DIR)/http.hpp $(INCLUDE_DIR)/string_utils.hpp
	@echo "$(CYAN)🧪 Building response unit tests...$(RESET)"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(RESP_BIN) $(RESP_SRC)

# Show help
help:
	@echo "$(CYAN)📚 WebServ Build Targets:$(RESET)"
	@echo "  🔨 make all     - Build the webserv executable (default)"
	@echo "  🧹 make clean   - Remove object files"
	@echo "  🗑️  make fclean  - Remove object files and executable"
	@echo "  🔄 make re      - Clean rebuild"
	@echo "  🧪 make unit    - Build and run unit tests"
	@echo "  📚 make help    - Show this help message"

# Phony targets (not files)
.PHONY: all clean fclean re help unit
