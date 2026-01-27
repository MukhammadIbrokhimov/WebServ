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
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Clean object files
clean:
	@echo "$(RED)🧹 Cleaning object files...$(RESET)"
	@rm -rf $(OBJ_DIR)
	@echo "$(GREEN)✅ Clean complete!$(RESET)"

# Full clean (objects + executable)
fclean: clean
	@echo "$(RED)🗑️  Removing executable...$(RESET)"
	@rm -f $(TARGET)
	@echo "$(GREEN)✅ Full clean complete!$(RESET)"

# Rebuild
re: fclean all

# Show help
help:
	@echo "$(CYAN)📚 WebServ Build Targets:$(RESET)"
	@echo "  🔨 make all     - Build the webserv executable (default)"
	@echo "  🧹 make clean   - Remove object files"
	@echo "  🗑️  make fclean  - Remove object files and executable"
	@echo "  🔄 make re      - Clean rebuild"
	@echo "  📚 make help    - Show this help message"

# Phony targets (not files)
.PHONY: all clean fclean re help