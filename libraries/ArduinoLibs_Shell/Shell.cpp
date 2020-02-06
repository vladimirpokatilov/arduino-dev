/*
 * Copyright (C) 2016 Southern Storm Software, Pty Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "Shell.h"
#include "LoginShell.h"
#include <string.h>
#include <stddef.h>

// Modes for line editing (flags).
#define LINEMODE_NORMAL     0x01
#define LINEMODE_ECHO       0x02
#define LINEMODE_USERNAME   0x04
#define LINEMODE_PASSWORD   0x08
#define LINEMODE_PROMPT     0x10
#define LINEMODE_DELAY      0x20

// Delay to insert after a failed login to slow down brute force attacks (ms).
#define LOGIN_SHELL_DELAY   3000

Shell::Shell()
    : curStart(0)
    , curLen(0)
    , curMax(sizeof(buffer))
    , history(0)
    , historyWrite(0)
    , historyRead(0)
    , historySize(0)
    , prom("$ ")
    , isClient(false)
    , lineMode(LINEMODE_NORMAL | LINEMODE_ECHO)
    , uid(-1)
    , timer(0)
{
}

Shell::~Shell()
{
    clearHistory();
    delete [] history;
}

bool Shell::begin(Stream &stream, size_t maxHistory, Terminal::Mode mode)
{
    if (!beginShell(stream, maxHistory, mode))
        return false;
    isClient = false;
    return true;
}

bool Shell::begin(Client &client, size_t maxHistory, Terminal::Mode mode)
{
    if (!beginShell(client, maxHistory, mode))
        return false;
    isClient = true;
    return true;
}

bool Shell::beginShell(Stream &stream, size_t maxHistory, Terminal::Mode mode)
{
    // Initialize the Terminal base class with the underlying stream.
    Terminal::begin(stream, mode);

    // Create the history buffer.
    bool ok = true;
    delete [] history;
    historySize = sizeof(buffer) * maxHistory;
    if (maxHistory) {
        history = new char [historySize];
        if (history) {
            memset(history, 0, historySize);
        } else {
            maxHistory = 0;
            historySize = 0;
            ok = false;
        }
    } else {
        history = 0;
    }

    // Clear other variables.
    curStart = 0;
    curLen = 0;
    curMax = sizeof(buffer);
    historyWrite = 0;
    historyRead = 0;
    uid = -1;

    // Begins the login session.
    beginSession();
    return ok;
}

void Shell::end()
{
    Terminal::end();
    clearHistory();
    delete [] history;
    curStart = 0;
    curLen = 0;
    curMax = sizeof(buffer);
    history = 0;
    historyWrite = 0;
    historyRead = 0;
    historySize = 0;
    isClient = false;
    lineMode = LINEMODE_NORMAL | LINEMODE_ECHO;
    uid = -1;
}

// Standard builtin command names.
static char const builtin_cmd_exit[] PROGMEM = "exit";
static char const builtin_cmd_help[] PROGMEM = "help";
static char const builtin_cmd_help_alt[] PROGMEM = "?";

void Shell::loop()
{
    // If the stream is a TCP client, then check for disconnection.
    if (isClient && !((Client *)stream())->connected()) {
        end();
        return;
    }

    // If the login delay is active, then suppress all input.
    if (lineMode & LINEMODE_DELAY) {
        if ((millis() - timer) >= LOGIN_SHELL_DELAY) {
            lineMode &= ~LINEMODE_DELAY;
            timer = 0;
        } else {
            readKey();
            return;
        }
    }

    // Print the prompt if necessary.
    if (lineMode & LINEMODE_PROMPT)
        printPrompt();

    // Read the next key and bail out if none.  We only process a single
    // key each time we enter this function to prevent other tasks in the
    // system from becoming starved of time resources if the bytes are
    // arriving rapidly from the underyling stream.
    int key = readKey();
    if (key == -1)
        return;

    // Process the key.
    switch (key) {
    case KEY_BACKSPACE:
        // Backspace over the last character.
        clearCharacters(1);
        break;

    case KEY_RETURN:
        // CR, LF, or CRLF pressed, so execute the current command.
        execute();
        break;

    case 0x15:
        // CTRL-U - clear the entire command.
        clearCharacters(curLen);
        break;

    case 0x04:
        // CTRL-D - equivalent to the "exit" command.
        if (lineMode & LINEMODE_NORMAL)
            executeBuiltin(builtin_cmd_exit);
        break;

    case KEY_UP_ARROW:
        // Go back one item in the command history.
        if ((lineMode & LINEMODE_NORMAL) != 0 &&
                history && historyRead > 0) {
            changeHistory(true);
        }
        break;

    case KEY_DOWN_ARROW:
        // Go forward one item in the command history.
        if ((lineMode & LINEMODE_NORMAL) != 0 &&
                history && historyRead < historyWrite) {
            changeHistory(false);
        }
        break;

    case KEY_F1:
        // F1 is equivalent to the "help" command.
        if (lineMode & LINEMODE_NORMAL)
            executeBuiltin(builtin_cmd_help);
        break;

    case KEY_UNICODE: {
        // Add the Unicode code point to the buffer if it will fit.
        long code = unicodeKey();
        size_t size = Terminal::utf8Length(code);
        if (size && (curLen + size) < (curMax - 1)) {
            Terminal::utf8Format((uint8_t *)(buffer + curLen), code);
            if (lineMode & LINEMODE_ECHO)
                write((uint8_t *)(buffer + curLen), size);
            curLen += size;
        }
    } break;

    default:
        if (key >= 0x20 && key <= 0x7E) {
            // Printable ASCII character - echo and add it to the buffer.
            if (curLen < (curMax - 1)) {
                if (lineMode & LINEMODE_ECHO)
                    write((uint8_t)key);
                buffer[curLen++] = (char)key;
            }
        }
        break;
    }
}

#if defined(__AVR__)

// String compare of two strings in program memory.
static int progmem_strcmp(const char *str1, const char *str2)
{
    uint8_t ch1, ch2;
    for (;;) {
        ch1 = pgm_read_byte((const uint8_t *)str1);
        ch2 = pgm_read_byte((const uint8_t *)str2);
        if (!ch1) {
            if (ch2)
                return -1;
            else
                break;
        } else if (!ch2) {
            return 1;
        } else if (ch1 != ch2) {
            return ((int)ch1) - ((int)ch2);
        }
        ++str1;
        ++str2;
    }
    return 0;
}

#else

#define progmem_strcmp(str1,str2) (strcmp((str1), (str2)))

#endif

// Reads the "name" field from a command information block in program memory.
static const char *readInfoName(const ShellCommandInfo *info)
{
#if defined(__AVR__)
    return (const char *)pgm_read_word
        (((const uint8_t *)info) + offsetof(ShellCommandInfo, name));
#else
    return info->name;
#endif
}

// Reads the "help" field from a command information block in program memory.
static const char *readInfoHelp(const ShellCommandInfo *info)
{
#if defined(__AVR__)
    return (const char *)pgm_read_word
        (((const uint8_t *)info) + offsetof(ShellCommandInfo, help));
#else
    return info->help;
#endif
}

// Reads the "func" field from a command information block in program memory.
static ShellCommandFunc readInfoFunc(const ShellCommandInfo *info)
{
#if defined(__AVR__)
    if (sizeof(ShellCommandFunc) == 2) {
        return (ShellCommandFunc)pgm_read_word
            (((const uint8_t *)info) + offsetof(ShellCommandInfo, func));
    } else {
        return (ShellCommandFunc)pgm_read_dword
            (((const uint8_t *)info) + offsetof(ShellCommandInfo, func));
    }
#else
    return info->func;
#endif
}

static ShellCommandRegister *firstCmd = 0;

void Shell::registerCommand(ShellCommandRegister *cmd)
{
    // Insert the command into the list in alphanumeric order.
    // We cannot rely upon the construction order to sort the list for us.
    ShellCommandRegister *prev = 0;
    ShellCommandRegister *current = firstCmd;
    while (current != 0) {
        if (progmem_strcmp(readInfoName(cmd->info), readInfoName(current->info)) < 0)
            break;
        prev = current;
        current = current->next;
    }
    if (prev)
        prev->next = cmd;
    else
        firstCmd = cmd;
    cmd->next = current;
}

void Shell::help()
{
    // Find the command with the maximum length.
    ShellCommandRegister *current = firstCmd;
    size_t maxLen = 0;
    size_t len;
    while (current != 0) {
        len = strlen_P(readInfoName(current->info));
        if (len > maxLen)
            maxLen = len;
        current = current->next;
    }
    maxLen += 2;

    // Print the commands with the help strings aligned on the right.
    current = firstCmd;
    while (current != 0) {
        writeProgMem(readInfoName(current->info));
        len = maxLen - strlen_P(readInfoName(current->info));
        while (len > 0) {
            write(' ');
            --len;
        }
        writeProgMem(readInfoHelp(current->info));
        println();
        current = current->next;
    }
}

void Shell::exit()
{
    Stream *stream = this->stream();
    uid = -1;
    if (isClient) {
        end();
        ((Client *)stream)->stop();
    } else {
        clearHistory();
        println();
        beginSession();
    }
}

void Shell::beginSession()
{
    // No login support in the base class, so enter normal mode immediately.
    lineMode = LINEMODE_NORMAL | LINEMODE_ECHO | LINEMODE_PROMPT;
}

void Shell::printPrompt()
{
    if (prom)
        print(prom);
    lineMode &= ~LINEMODE_PROMPT;
}

void Shell::execute()
{
    // Terminate the current line.
    println();

    // Make sure the command is properly NUL-terminated.
    buffer[curLen] = '\0';

    // If we have a history stack and the new command is different from
    // the previous command, then copy the command into the stack.
    if (history && curLen > curStart) {
        char *prevCmd;
        bool newCmd = true;
        if (historyWrite > 0) {
            prevCmd = (char *)memrchr(history, '\0', historyWrite - 1);
            if (prevCmd)
                ++prevCmd;
            else
                prevCmd = history;
            if (strcmp(prevCmd, buffer + curStart) == 0)
                newCmd = false;
        }
        if (newCmd) {
            size_t len = curLen - curStart;
            while ((len + 1) > (historySize - historyWrite)) {
                // History stack is full.  Pop older entries to get some room.
                prevCmd = (char *)memchr(history, '\0', historyWrite);
                if (prevCmd) {
                    size_t histLen = historyWrite - ((prevCmd + 1) - history);
                    memmove(history, prevCmd + 1, histLen);
                    historyWrite = histLen;
                } else {
                    historyWrite = 0;
                    break;
                }
            }
            memcpy(history + historyWrite, buffer + curStart, len);
            historyWrite += len;
            history[historyWrite++] = '\0';
        }
    }

    // Reset the history read position to the top of the stack.
    historyRead = historyWrite;

    // Break the command up into arguments and populate the argument array.
    ShellArguments argv(buffer + curStart, curLen - curStart);

    // Clear the line buffer.
    curLen = curStart;

    // Execute the command.
    if (argv.count() > 0) {
        if (!execute(argv)) {
            // Could not find a matching command, try the builtin "help".
            const char *argv0 = argv[0];
            if (!strcmp_P(argv0, builtin_cmd_help) ||
                    !strcmp_P(argv0, builtin_cmd_help_alt)) {
                help();
            } else if (!strcmp_P(argv0, builtin_cmd_exit)) {
                exit();
            } else {
                static char const unknown_cmd[] PROGMEM = "Unknown command: ";
                writeProgMem(unknown_cmd);
                print(argv0);
                println();
            }
        }
    }

    // Prepare to print the prompt for the next command.
    lineMode |= LINEMODE_PROMPT;
}

bool Shell::execute(const ShellArguments &argv)
{
    const char *argv0 = argv[0];
    ShellCommandRegister *current = firstCmd;
    while (current != 0) {
        if (!strcmp_P(argv0, readInfoName(current->info))) {
            ShellCommandFunc func = readInfoFunc(current->info);
            (*func)(*this, argv.count(), argv);
            return true;
        }
        current = current->next;
    }
    return false;
}

void Shell::executeBuiltin(const char *cmd)
{
    clearCharacters(curLen);
    curLen = strlen_P(cmd);
    strncpy_P(buffer + curStart, cmd, curLen);
    write((const uint8_t *)(buffer + curStart), curLen);
    curLen += curStart;
    execute();
}

void Shell::clearCharacters(size_t len)
{
    // If the characters are hidden, then there's nothing to backspace over.
    if (!(lineMode & LINEMODE_ECHO))
        return;

    // Backspace over all characters in the buffer.
    while (len > 0 && curLen > curStart) {
        uint8_t ch = (uint8_t)(buffer[curLen - 1]);
        if (ch < 0x80) {
            backspace();
        } else {
            // UTF-8 character sequence.  Back up some more and
            // determine the value of the Unicode code point.
            long code = (ch & 0x3F);
            uint8_t shift = 6;
            while (curLen > 1) {
                --curLen;
                ch = (uint8_t)(buffer[curLen - 1]);
                if ((ch & 0xC0) != 0x80)
                    break;
                code |= ((long)(ch & 0x3F)) << shift;
                shift += 6;
            }
            if ((ch & 0xE0) == 0xC0)
                ch &= 0x1F;
            else if ((ch & 0xF0) == 0xE0)
                ch &= 0x0F;
            else
                ch &= 0x07;
            code |= ((long)ch) << shift;

            // If the character is wide, we need to emit two backspaces.
            if (isWideCharacter(code))
                backspace();
            backspace();
        }
        --len;
        --curLen;
    }
}

void Shell::changeHistory(bool up)
{
    char *cmd;
    if (up) {
        cmd = (char *)memrchr(history, '\0', historyRead - 1);
        if (cmd)
            historyRead = (size_t)(cmd - history + 1);
        else
            historyRead = 0;
    } else {
        cmd = (char *)memchr(history + historyRead, '\0', historyWrite - historyRead);
        if (cmd)
            historyRead = (size_t)(cmd - history + 1);
        else
            historyRead = historyWrite;
    }
    clearCharacters(curLen);
    if (historyRead < historyWrite) {
        cmd = history + historyRead;
        curLen = strlen(cmd);
        if (curLen > (curMax - curStart))
            curLen = curMax - curStart;
        memcpy(buffer + curStart, cmd, curLen);
        write((uint8_t *)cmd, curLen);
        curLen += curStart;
    }
}

void Shell::clearHistory()
{
    if (history)
        memset(history, 0, historySize);
    historyRead = 0;
    historyWrite = 0;
    memset(buffer, 0, sizeof(buffer));
}

ShellArguments::ShellArguments(char *buffer, size_t len)
    : line(buffer)
    , size(0)
    , argc(0)
    , currentIndex(0)
    , currentPosn(0)
{
    // Break the command up into arguments and add NUL terminators.
    size_t posn = 0;
    size_t outposn = 0;
    char quote = 0;
    while (posn < len) {
        char ch = buffer[posn];
        if (ch == ' ') {
            ++posn;
            continue;
        }
        ++argc;
        do {
            ch = buffer[posn];
            if (ch == '"' || ch == '\'') {
                if (quote == ch) {
                    quote = 0;
                    ++posn;
                    continue;
                } else if (!quote) {
                    quote = ch;
                    ++posn;
                    continue;
                }
            } else if (!quote && ch == ' ') {
                break;
            }
            buffer[outposn++] = ch;
            ++posn;
        } while (posn < len);
        buffer[outposn++] = '\0';
        if (posn < len)
            ++posn;
    }
    size = outposn;
}

const char *ShellArguments::operator[](int index) const
{
    if (index < 0 || index >= argc) {
        // Argument index is out of range.
        return 0;
    } else if (index == currentIndex) {
        // We already found this argument last time.
        return line + currentPosn;
    } else {
        // Search forwards or backwards for the next argument.
        const char *temp;
        while (index > currentIndex) {
            temp = (const char *)memchr
                (line + currentPosn, '\0', size - currentPosn);
            if (!temp)
                return 0;
            currentPosn = ((size_t)(temp - line)) + 1;
            ++currentIndex;
        }
        while (index < currentIndex) {
            temp = (const char *)memrchr(line, '\0', currentPosn - 1);
            if (temp)
                currentPosn = ((size_t)(temp - line)) + 1;
            else
                currentPosn = 0;
            --currentIndex;
        }
        return line + currentPosn;
    }
}

void LoginShell::beginSession()
{
    lineMode = LINEMODE_USERNAME | LINEMODE_ECHO | LINEMODE_PROMPT;
    curStart = 0;
    curLen = 0;
    curMax = sizeof(buffer) / 2;
}

void LoginShell::printPrompt()
{
    static char const loginString[] PROGMEM = "login: ";
    static char const passwordString[] PROGMEM = "Password: ";
    if (lineMode & LINEMODE_NORMAL) {
        // Print the prompt for normal command entry.
        if (prom)
            print(prom);

        // Normal commands occupy the full command buffer.
        curStart = 0;
        curLen = 0;
        curMax = sizeof(buffer);
    } else if (lineMode & LINEMODE_USERNAME) {
        // Print the machine name and the login prompt.
        if (machName) {
            print(machName);
            write((uint8_t)' ');
        }
        writeProgMem(loginString);

        // Login name is placed into the first half of the line buffer.
        curStart = 0;
        curLen = 0;
        curMax = sizeof(buffer) / 2;
    } else if (lineMode & LINEMODE_PASSWORD) {
        // Print the password prompt.
        writeProgMem(passwordString);

        // Password is placed into the second half of the line buffer.
        curStart = sizeof(buffer) / 2;
        curLen = curStart;
        curMax = sizeof(buffer);
    }
    lineMode &= ~LINEMODE_PROMPT;
}

// Default password checking function.  This is not a very good security check!
static int defaultPasswordCheckFunc(const char *username, const char *password)
{
    static char const defaultUsername[] PROGMEM = "root";
    static char const defaultPassword[] PROGMEM = "arduino";
    if (!strcmp_P(username, defaultUsername) &&
            !strcmp_P(password, defaultPassword)) {
        return 0;
    } else {
        return -1;
    }
}

void LoginShell::execute()
{
    if (lineMode & LINEMODE_NORMAL) {
        // Normal command execution.
        Shell::execute();
    } else if (lineMode & LINEMODE_USERNAME) {
        // Prompting for the login username.
        buffer[curLen] = '\0';
        lineMode = LINEMODE_PASSWORD | LINEMODE_PROMPT;
        println();
    } else if (lineMode & LINEMODE_PASSWORD) {
        // Prompting for the login password.
        buffer[curLen] = '\0';
        println();

        // Check the user name and password.
        int userid;
        if (checkFunc)
            userid = checkFunc(buffer, buffer + sizeof(buffer) / 2);
        else
            userid = defaultPasswordCheckFunc(buffer, buffer + sizeof(buffer) / 2);

        // Clear the user name and password from memory after they are checked.
        memset(buffer, 0, sizeof(buffer));

        // Go to either normal mode or back to username mode.
        if (userid >= 0) {
            uid = userid;
            lineMode = LINEMODE_NORMAL | LINEMODE_ECHO | LINEMODE_PROMPT;
        } else {
            lineMode = LINEMODE_USERNAME | LINEMODE_ECHO |
                       LINEMODE_PROMPT | LINEMODE_DELAY;
            timer = millis();
        }
    }
}