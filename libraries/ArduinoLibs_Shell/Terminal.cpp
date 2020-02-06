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

#include "Terminal.h"
#include "TelnetDefs.h"

// States for the key recognition state machine.
#define STATE_INIT      0       // Initial state.
#define STATE_CR        1       // Last character was CR, eat following LF.
#define STATE_ESC       2       // Last character was ESC.
#define STATE_MATCH     3       // Matching an escape sequence.
#define STATE_UTF8      4       // Recognizing a UTF-8 sequence.
#define STATE_IAC       5       // Recognizing telnet command after IAC (0xFF).
#define STATE_WILL      6       // Waiting for option code for WILL command.
#define STATE_WONT      7       // Waiting for option code for WONT command.
#define STATE_DO        8       // Waiting for option code for DO command.
#define STATE_DONT      9       // Waiting for option code for DONT command.
#define STATE_SB        10      // Option sub-negotiation.
#define STATE_SB_IAC    11      // Option sub-negotiation, byte after IAC.

// Number of milliseconds to wait after an ESC character before
// concluding that it is KEY_ESC rather than an escape sequence.
#define ESC_TIMEOUT_MS  40

// Number of milliseconds to wait for a new character within an
// escape sequence before concluding that the sequence was invalid
// or truncated, or not actually an escape sequence at all.
#define SEQ_TIMEOUT_MS  200

Terminal::Terminal()
    : _stream(0)
    , ucode(-1)
    , ncols(80)
    , nrows(24)
    , timer(0)
    , offset(0)
    , state(STATE_INIT)
    , utf8len(0)
    , mod(Terminal::Serial)
    , flags(0)
{
}

Terminal::~Terminal()
{
}

void Terminal::begin(Stream &stream, Mode mode)
{
    _stream = &stream;
    ucode = -1;
    state = STATE_INIT;
    flags = 0;
    mod = mode;
}

void Terminal::end()
{
    _stream = 0;
}

int Terminal::available()
{
    return _stream ? _stream->available() : 0;
}

int Terminal::peek()
{
    return _stream ? _stream->peek() : -1;
}

int Terminal::read()
{
    // Clear the key recognition state because we are bypassing readKey().
    state = STATE_INIT;
    ucode = -1;

    // Read the next byte from the underlying stream.
    return _stream ? _stream->read() : -1;
}

void Terminal::flush()
{
    if (_stream)
        _stream->flush();
}

size_t Terminal::write(uint8_t c)
{
    return _stream ? _stream->write(c) : 0;
}

size_t Terminal::write(const uint8_t *buffer, size_t size)
{
    return _stream ? _stream->write(buffer, size) : 0;
}

void Terminal::writeProgMem(const char *str)
{
    uint8_t buffer[16];
    uint8_t posn;
    uint8_t ch;
    if (!_stream || !str)
        return;
    posn = 0;
    while ((ch = pgm_read_byte((const uint8_t *)str)) != 0) {
        buffer[posn++] = ch;
        if (posn == sizeof(buffer)) {
            _stream->write(buffer, posn);
            posn = 0;
        }
        ++str;
    }
    if (posn != 0)
        _stream->write(buffer, posn);
}

static bool escapeSequenceStart(int ch)
{
    if (ch == '[' || ch == '?')
        return true;
    else if (ch >= 'A' && ch <= 'Z')
        return true;
    else
        return false;
}

int Terminal::readKey()
{
    int ch;

    // Bail out if there is no underlying stream.
    if (!_stream)
        return -1;

    // Read the next character and bail out if nothing yet.  Some special
    // peek-ahead handling is needed just after the ESC character.
    if (state == STATE_ESC) {
        ch = _stream->peek();
        if (ch < 0) {
            // We just saw an ESC.  If there has been a timeout
            // then the key is KEY_ESC rather than the start of a
            // VT100 escape sequence.
            if ((millis() - timer) >= ESC_TIMEOUT_MS) {
                state = STATE_INIT;
                ucode = 0x1B;
                return KEY_ESC;
            }
            ucode = -1;
            return -1;
        } else if (!escapeSequenceStart(ch)) {
            // The next character is not legitimate as the start of
            // an escape sequence, so the ESC must have been KEY_ESC.
            state = STATE_INIT;
            ucode = 0x1B;
            return KEY_ESC;
        } else {
            // Part of an escape sequence.  Read the character properly.
            ch = _stream->read();
        }
    } else {
        // Read the next character without any peek-ahead.
        ch = _stream->read();
    }
    if (ch < 0) {
        if (state == STATE_MATCH && (millis() - timer) >= SEQ_TIMEOUT_MS) {
            // Timeout while waiting for the next character in an
            // escape sequence.  Abort and return to the initial state.
            state = STATE_INIT;
        }
        ucode = -1;
        return -1;
    }

    // Determine what to do based on the key recognition state.
    switch (state) {
    case STATE_CR:
        // We just saw a CR, so check for CRLF and eat the LF.
        state = STATE_INIT;
        if (ch == 0x0A) {
            ucode = -1;
            return -1;
        } else if (ch == 0x00 && mod == Telnet) {
            // In telnet mode, CR NUL is a literal carriage return,
            // separate from the newline sequence CRLF.  Eat the NUL.
            // We already reported KEY_RETURN for the CR character.
            ucode = -1;
            return -1;
        }
        // Fall through to the next case.

    case STATE_INIT:
        if (ch >= 0x20 && ch <= 0x7E) {
            // Printable ASCII character.
            state = STATE_INIT;
            ucode = ch;
            return ch;
        } else if (ch == 0x1B) {
            // Start of an escape sequence, or the escape character itself.
            state = STATE_ESC;
            timer = millis();
        } else if (ch == 0x0D) {
            // CR which may be followed by an LF.
            state = STATE_CR;
            ucode = ch;
            return KEY_RETURN;
        } else if (ch == 0x0A) {
            // LF on its own without a preceding CR.
            ucode = ch;
            return KEY_RETURN;
        } else if (ch == 0x08 || ch == 0x7F) {
            // Backspace or DEL character.
            state = STATE_INIT;
            ucode = ch;
            return KEY_BACKSPACE;
        } else if (ch == 0x09) {
            // TAB character.
            state = STATE_INIT;
            ucode = ch;
            return KEY_TAB;
        } else if (ch < 0x80) {
            // Some other ASCII control character.
            state = STATE_INIT;
            ucode = ch;
            return ch;
        } else if (ch >= 0xC1 && ch <= 0xDF) {
            // Two-byte UTF-8 sequence.
            offset = ch & 0x1F;
            utf8len = 2;
            state = STATE_UTF8;
        } else if (ch >= 0xE1 && ch <= 0xEF) {
            // Three-byte UTF-8 sequence.
            offset = ch & 0x0F;
            utf8len = 3;
            state = STATE_UTF8;
        } else if (ch >= 0xF1 && ch <= 0xF7) {
            // Four-byte UTF-8 sequence.
            offset = ch & 0x07;
            utf8len = 4;
            state = STATE_UTF8;
        } else if (ch == 0xFF && mod == Telnet) {
            // Start of a telnet command (IAC byte).
            state = STATE_IAC;
        }
        break;

    case STATE_ESC:
        // Next character just after the ESC.  Start the escape
        // sequence matching engine at offset zero in the keymap table.
        state = STATE_MATCH;
        offset = 0;
        // Fall through to the next case.

    case STATE_MATCH:
        // In the middle of matching an escape sequence.
        if (ch == 0x1B) {
            // ESC character seen in the middle of an escape sequence.
            // The previous escape sequence is invalid so abort and restart.
            state = STATE_ESC;
            timer = millis();
            break;
        }
        ch = matchEscape(ch);
        if (ch == -1) {
            // Need more characters before knowing what this is.
            timer = millis();
        } else if (ch == -2) {
            // Invalid escape sequence so abort and restart.
            state = STATE_INIT;
        } else if (ch < 0x80) {
            // Escape sequence corresponds to a normal ASCII character.
            state = STATE_INIT;
            ucode = ch;
            return ch;
        } else {
            // Extended keycode for an arrow or function key.
            state = STATE_INIT;
            ucode = -1;
            return ch;
        }
        break;

    case STATE_UTF8:
        // Recognize a multi-byte UTF-8 character encoding.
        if ((ch & 0xC0) == 0x80) {
            if (utf8len <= 2) {
                // Final character in the sequence.
                ucode = (((long)offset) << 6) | (ch & 0x3F);
                state = STATE_INIT;
                if (ucode > 0x10FFFFL)
                    break;      // The code point is out of range.
                return KEY_UNICODE;
            } else {
                // More characters still yet to come.
                --utf8len;
                offset = (offset << 6) | (ch & 0x3F);
            }
        } else {
            // This character is invalid as part of a UTF-8 sequence.
            state = STATE_INIT;
        }
        break;

    case STATE_IAC:
        // Telnet command byte just after an IAC (0xFF) character.
        switch (ch) {
        case TelnetDefs::EndOfFile:
            // Convert EOF into CTRL-D.
            state = STATE_INIT;
            ucode = 0x04;
            return 0x04;

        case TelnetDefs::EndOfRecord:
            // Convert end of record markers into CR.
            state = STATE_INIT;
            ucode = 0x0D;
            return KEY_RETURN;

        case TelnetDefs::Interrupt:
            // Convert interrupt into CTRL-C.
            state = STATE_INIT;
            ucode = 0x03;
            return 0x03;

        case TelnetDefs::EraseChar:
            // Convert erase character into DEL.
            state = STATE_INIT;
            ucode = 0x7F;
            return KEY_BACKSPACE;

        case TelnetDefs::EraseLine:
            // Convert erase line into CTRL-U.
            state = STATE_INIT;
            ucode = 0x15;
            return 0x15;

        case TelnetDefs::SubStart:
            // Option sub-negotiation.
            utf8len = 0;
            state = STATE_SB;
            break;

        case TelnetDefs::WILL:
            // Option negotiation, WILL command.
            state = STATE_WILL;
            break;

        case TelnetDefs::WONT:
            // Option negotiation, WONT command.
            state = STATE_WONT;
            break;

        case TelnetDefs::DO:
            // Option negotiation, DO command.
            state = STATE_DO;
            break;

        case TelnetDefs::DONT:
            // Option negotiation, DONT command.
            state = STATE_DONT;
            break;

        case TelnetDefs::IAC:
            // IAC followed by IAC is the literal byte 0xFF,
            // but that isn't valid UTF-8 so we just drop it.
            state = STATE_INIT;
            break;

        default:
            // Everything else is treated as a NOP.
            state = STATE_INIT;
            break;
        }
        break;

    case STATE_WILL:
        // Telnet option negotiation, WILL command.  Note: We don't do any
        // loop detection.  We assume that the client will eventually break
        // the loop as it probably has more memory than us to store state.
        if (ch == TelnetDefs::WindowSize ||
                ch == TelnetDefs::RemoteFlowControl) {
            // Send a DO command in response - we accept this option.
            telnetCommand(TelnetDefs::DO, ch);
        } else {
            // Send a DONT command in response - we don't accept this option.
            telnetCommand(TelnetDefs::DONT, ch);
        }
        if (!(flags & 0x01)) {
            // The first time we see a WILL command from the client we
            // send a request back saying that we will handle echoing.
            flags |= 0x01;
            telnetCommand(TelnetDefs::WILL, TelnetDefs::Echo);
        }
        state = STATE_INIT;
        break;

    case STATE_WONT:
    case STATE_DONT:
        // Telnet option negotiation, WONT/DONT command.  The other side
        // is telling us that it does not understand this option or wants
        // us to stop using it.  For now there is nothing to do.
        state = STATE_INIT;
        break;

    case STATE_DO:
        // Telnet option negotiation, DO command.  Note: Other than Echo
        // we don't do any loop detection.  We assume that the client will
        // break the loop as it probably has more memory than us to store state.
        if (ch == TelnetDefs::Echo) {
            // Special handling needed for Echo - don't say WILL again
            // when the client acknowledges us with a DO command.
        } else if (ch == TelnetDefs::SuppressGoAhead) {
            // Send a WILL command in response - we accept this option.
            telnetCommand(TelnetDefs::WILL, ch);
        } else {
            // Send a WONT command in response - we don't accept this option.
            telnetCommand(TelnetDefs::WONT, ch);
        }
        state = STATE_INIT;
        break;

    case STATE_SB:
        // Telnet option sub-negotiation.  Collect up all bytes and
        // then execute the option once "IAC SubEnd" is seen.
        if (ch == TelnetDefs::IAC) {
            // IAC byte, which will be followed by either IAC or SubEnd.
            state = STATE_SB_IAC;
            break;
        }
        if (utf8len < sizeof(sb))
            sb[utf8len++] = ch;
        break;

    case STATE_SB_IAC:
        // Telnet option sub-negotiation, byte after IAC.
        if (ch == TelnetDefs::IAC) {
            // Two IAC bytes in a row is a single escaped 0xFF byte.
            if (utf8len < sizeof(sb))
                sb[utf8len++] = 0xFF;
            state = STATE_SB;
            break;
        } else if (ch == TelnetDefs::SubEnd) {
            // End of the sub-negotiation field.  Handle window size changes.
            if (utf8len >= 5 && sb[0] == TelnetDefs::WindowSize) {
                int width  = (((int)(sb[1])) << 8) | sb[2];
                int height = (((int)(sb[3])) << 8) | sb[4];
                if (!width)         // Zero width or height means "unspecified".
                    width = ncols;
                if (!height)
                    height = nrows;
                if (setWindowSize(width, height)) {
                    // The window size has changed; notify the caller.
                    ucode = -1;
                    state = STATE_INIT;
                    return KEY_WINSIZE;
                }
            }
        }
        state = STATE_INIT;
        break;
    }

    // If we get here, then we're still waiting for a full sequence.
    ucode = -1;
    return -1;
}

size_t Terminal::writeUnicode(long code)
{
    uint8_t utf8[4];
    size_t size = utf8Format(utf8, code);
    if (size > 0)
        write(utf8, size);
    return size;
}

bool Terminal::setWindowSize(int columns, int rows)
{
    // Sanity-check the range first.
    if (columns < 1)
        columns = 1;
    else if (columns > 10000)
        columns = 10000;
    if (rows < 1)
        rows = 1;
    else if (rows > 10000)
        rows = 10000;
    if (ncols != columns || nrows != rows) {
        ncols = columns;
        nrows = rows;
        return true;
    } else {
        return false;
    }
}

void Terminal::clear()
{
    static char const escape[] PROGMEM = "\033[H\033[J";
    writeProgMem(escape);
}

void Terminal::clearToEOL()
{
    static char const escape[] PROGMEM = "\033[K";
    writeProgMem(escape);
}

// Writes a decimal number to a buffer.
static void writeNumber(uint8_t *buf, uint8_t &posn, int value)
{
    int divisor = 10000;
    bool haveDigits = false;
    while (divisor >= 1) {
        int digit = value / divisor;
        if (digit || haveDigits) {
            buf[posn++] = '0' + digit;
            haveDigits = true;
        }
        value %= divisor;
        divisor /= 10;
    }
    if (!haveDigits) {
        buf[posn++] = '0';
    }
}

void Terminal::cursorMove(int x, int y)
{
    if (!_stream)
        return;

    // Range check the arguments.
    if (x < 0)
        x = 0;
    else if (x >= ncols)
        x = ncols - 1;
    if (y < 0)
        y = 0;
    else if (y >= nrows)
        y = nrows - 1;

    // Format the command "ESC[row;colH" and send it.
    uint8_t buffer[16];
    uint8_t posn = 0;
    buffer[posn++] = 0x1B;
    buffer[posn++] = '[';
    writeNumber(buffer, posn, y + 1);
    buffer[posn++] = ';';
    writeNumber(buffer, posn, x + 1);
    buffer[posn++] = 'H';
    _stream->write(buffer, posn);
}

void Terminal::cursorLeft()
{
    static char const escape[] PROGMEM = "\033[D";
    writeProgMem(escape);
}

void Terminal::cursorRight()
{
    static char const escape[] PROGMEM = "\033[C";
    writeProgMem(escape);
}

void Terminal::cursorUp()
{
    static char const escape[] PROGMEM = "\033[A";
    writeProgMem(escape);
}

void Terminal::cursorDown()
{
    static char const escape[] PROGMEM = "\033[B";
    writeProgMem(escape);
}

void Terminal::backspace()
{
    static char const escape[] PROGMEM = "\b \b";
    writeProgMem(escape);
}

void Terminal::insertLine()
{
    static char const escape[] PROGMEM = "\033[L";
    writeProgMem(escape);
}

void Terminal::insertChar()
{
    static char const escape[] PROGMEM = "\033[@";
    writeProgMem(escape);
}

void Terminal::deleteLine()
{
    static char const escape[] PROGMEM = "\033[M";
    writeProgMem(escape);
}

void Terminal::deleteChar()
{
    static char const escape[] PROGMEM = "\033[P";
    writeProgMem(escape);
}

void Terminal::scrollUp()
{
    static char const escape[] PROGMEM = "\033[S";
    writeProgMem(escape);
}

void Terminal::scrollDown()
{
    static char const escape[] PROGMEM = "\033[T";
    writeProgMem(escape);
}

void Terminal::normal()
{
    static char const escape[] PROGMEM = "\033[0m";
    writeProgMem(escape);
}

void Terminal::bold()
{
    static char const escape[] PROGMEM = "\033[1m";
    writeProgMem(escape);
}

void Terminal::underline()
{
    static char const escape[] PROGMEM = "\033[4m";
    writeProgMem(escape);
}

void Terminal::blink()
{
    static char const escape[] PROGMEM = "\033[5m";
    writeProgMem(escape);
}

void Terminal::reverse()
{
    static char const escape[] PROGMEM = "\033[7m";
    writeProgMem(escape);
}

void Terminal::color(Color fg)
{
    uint8_t code = (fg & 0x07);
    uint8_t bold = (fg & 0x08) ? 1 : 0;
    if (!_stream)
        return;
    uint8_t buffer[16];
    uint8_t posn = 0;
    buffer[posn++] = 0x1B;
    buffer[posn++] = '[';
    buffer[posn++] = '0';   // reset all attributes first
    buffer[posn++] = ';';
    buffer[posn++] = '3';
    buffer[posn++] = '0' + code;
    if (bold) {
        buffer[posn++] = ';';
        buffer[posn++] = '1';
    }
    buffer[posn++] = 'm';
    _stream->write(buffer, posn);
}

void Terminal::color(Color fg, Color bg)
{
    uint8_t codefg = (fg & 0x07);
    uint8_t boldfg = (fg & 0x08) ? 1 : 0;
    uint8_t codebg = (bg & 0x07);
    if (!_stream)
        return;
    uint8_t buffer[16];
    uint8_t posn = 0;
    buffer[posn++] = 0x1B;
    buffer[posn++] = '[';
    buffer[posn++] = '0';   // reset all attributes first
    buffer[posn++] = ';';
    buffer[posn++] = '3';
    buffer[posn++] = '0' + codefg;
    if (boldfg) {
        buffer[posn++] = ';';
        buffer[posn++] = '1';
    }
    buffer[posn++] = ';';
    buffer[posn++] = '4';
    buffer[posn++] = '0' + codebg;
    buffer[posn++] = 'm';
    _stream->write(buffer, posn);
}

bool Terminal::isWideCharacter(long code)
{
    // This function was automatically generated by genwcwidth.c
    static unsigned char const range3000[32] PROGMEM = {
        0xF1, 0xFF, 0xF3, 0x3F, 0x01, 0x00, 0x01, 0x78,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88
    };
    static unsigned char const rangeFE00[64] PROGMEM = {
        0x00, 0x00, 0x80, 0x03, 0x00, 0x00, 0xE1, 0xFF,
        0x9F, 0x01, 0x00, 0x7F, 0x0C, 0x03, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x10, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8,
        0x01, 0x00, 0x00, 0xF8, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00
    };
    unsigned c;
    if (code < 0x2300) {
        return false;
    } else if (code >= 0x3000 && code <= 0x30FF) {
        c = (unsigned)(code - 0x3000);
        return (pgm_read_byte(range3000 + (c / 8)) & (1 << (c % 8))) != 0;
    } else if (code >= 0xFE00 && code <= 0xFFFF) {
        c = (unsigned)(code - 0xFE00);
        return (pgm_read_byte(rangeFE00 + (c / 8)) & (1 << (c % 8))) != 0;
    } else if (code >= 0x3400 && code <= 0x4DBF) {
        return true;
    } else if (code >= 0x4E00 && code <= 0x9FFF) {
        return true;
    } else if (code >= 0xF900 && code <= 0xFAFF) {
        return true;
    } else if (code >= 0x20000 && code <= 0x2FFFD) {
        return true;
    } else if (code >= 0x30000 && code <= 0x3FFFD) {
        return true;
    } else if (code == 0x2329 ||
               code == 0x232A ||
               code == 0x3250 ||
               code == 0xA015) {
        return true;
    }
    return false;
}
size_t Terminal::utf8Length(long code)
{
    // Reference: https://tools.ietf.org/html/rfc3629
    if (code < 0) {
        return 0;
    } else if (code <= 0x7FL) {
        return 1;
    } else if (code <= 0x07FFL) {
        return 2;
    } else if (code >= 0xD800L && code <= 0xDFFF) {
        // UTF-16 surrogate pairs are not valid in UTF-8.
        return 0;
    } else if (code <= 0xFFFFL) {
        return 3;
    } else if (code <= 0x10FFFFL) {
        return 4;
    } else {
        return 0;
    }
}

size_t Terminal::utf8Format(uint8_t *buffer, long code)
{
    // Reference: https://tools.ietf.org/html/rfc3629
    if (code < 0) {
        return 0;
    } else if (code <= 0x7FL) {
        buffer[0] = (uint8_t)code;
        return 1;
    } else if (code <= 0x07FFL) {
        buffer[0] = 0xC0 | (uint8_t)(code >> 6);
        buffer[1] = 0x80 | (((uint8_t)code) & 0x3F);
        return 2;
    } else if (code >= 0xD800L && code <= 0xDFFF) {
        // UTF-16 surrogate pairs are not valid in UTF-8.
        return 0;
    } else if (code <= 0xFFFFL) {
        buffer[0] = 0xE0 | (uint8_t)(code >> 12);
        buffer[1] = 0x80 | (((uint8_t)(code >> 6)) & 0x3F);
        buffer[2] = 0x80 | (((uint8_t)code) & 0x3F);
        return 3;
    } else if (code <= 0x10FFFFL) {
        buffer[0] = 0xF0 | (uint8_t)(code >> 18);
        buffer[1] = 0x80 | (((uint8_t)(code >> 12)) & 0x3F);
        buffer[2] = 0x80 | (((uint8_t)(code >> 6)) & 0x3F);
        buffer[3] = 0x80 | (((uint8_t)code) & 0x3F);
        return 4;
    } else {
        return 0;
    }
}

// Keymap rule table.  Compact representation of a recognition tree.
// Each tree node is an array of entries of the following forms:
//      0           End of this tree level.
//      ch code     Leaf node: ASCII character (bit 7 clear) plus 8-bit keycode.
//      ch offset   Interior node: ASCII character with the high bit set
//                  plus a 16-bit offset to the first child node.
// This table was generated with the "genkeymap" tool.  Do not edit this
// table but rather edit the tool and rebuild the table from it.
static uint8_t const keymap[459] PROGMEM = {
    0xDB, 0x1A, 0x00, 0xCF, 0x57, 0x01, 0x41, 0xDA, 0x42, 0xD9, 0x43, 0xD7,
    0x44, 0xD8, 0xBF, 0xA2, 0x01, 0x50, 0xC2, 0x51, 0xC3, 0x52, 0xC4, 0x53,
    0xC5, 0x00, 0x41, 0xDA, 0x42, 0xD9, 0x43, 0xD7, 0x44, 0xD8, 0x48, 0xD2,
    0xB1, 0x42, 0x00, 0x46, 0xD5, 0xB4, 0xC9, 0x00, 0xB2, 0xCC, 0x00, 0xB3,
    0x2B, 0x01, 0xB5, 0x46, 0x01, 0xB6, 0x49, 0x01, 0xDB, 0x4C, 0x01, 0x5A,
    0x0B, 0x50, 0xD0, 0x47, 0xE5, 0x00, 0x7E, 0xD2, 0xB1, 0x5D, 0x00, 0xB2,
    0x6C, 0x00, 0xB3, 0x7B, 0x00, 0xB4, 0x88, 0x00, 0xB5, 0x95, 0x00, 0xB7,
    0xA2, 0x00, 0xB8, 0xAF, 0x00, 0xB9, 0xBC, 0x00, 0x00, 0x7E, 0xC2, 0xBB,
    0x65, 0x00, 0x5E, 0xFA, 0x00, 0xB2, 0x69, 0x00, 0x00, 0x7E, 0xF0, 0x00,
    0x7E, 0xC3, 0xBB, 0x74, 0x00, 0x5E, 0xFB, 0x00, 0xB2, 0x78, 0x00, 0x00,
    0x7E, 0xF1, 0x00, 0x7E, 0xC4, 0xBB, 0x81, 0x00, 0x00, 0xB2, 0x85, 0x00,
    0x00, 0x7E, 0xF2, 0x00, 0x7E, 0xC5, 0xBB, 0x8E, 0x00, 0x00, 0xB2, 0x92,
    0x00, 0x00, 0x7E, 0xF3, 0x00, 0x7E, 0xC6, 0xBB, 0x9B, 0x00, 0x00, 0xB2,
    0x9F, 0x00, 0x00, 0x7E, 0xF4, 0x00, 0x7E, 0xC7, 0xBB, 0xA8, 0x00, 0x00,
    0xB2, 0xAC, 0x00, 0x00, 0x7E, 0xF5, 0x00, 0x7E, 0xC8, 0xBB, 0xB5, 0x00,
    0x00, 0xB2, 0xB9, 0x00, 0x00, 0x7E, 0xF6, 0x00, 0x7E, 0xC9, 0xBB, 0xC2,
    0x00, 0x00, 0xB2, 0xC6, 0x00, 0x00, 0x7E, 0xF7, 0x00, 0x7E, 0xD5, 0x00,
    0x7E, 0xD1, 0xB0, 0xE7, 0x00, 0xB1, 0xF4, 0x00, 0xB3, 0x01, 0x01, 0xB4,
    0x10, 0x01, 0xB5, 0x1F, 0x01, 0xB6, 0x22, 0x01, 0xB8, 0x25, 0x01, 0xB9,
    0x28, 0x01, 0x00, 0x7E, 0xCA, 0xBB, 0xED, 0x00, 0x00, 0xB2, 0xF1, 0x00,
    0x00, 0x7E, 0xF8, 0x00, 0x7E, 0xCB, 0xBB, 0xFA, 0x00, 0x00, 0xB2, 0xFE,
    0x00, 0x00, 0x7E, 0xF9, 0x00, 0x7E, 0xCC, 0x24, 0xF8, 0xBB, 0x09, 0x01,
    0x00, 0xB2, 0x0D, 0x01, 0x00, 0x7E, 0xFA, 0x00, 0x7E, 0xCD, 0x24, 0xF9,
    0xBB, 0x18, 0x01, 0x00, 0xB2, 0x1C, 0x01, 0x00, 0x7E, 0xFB, 0x00, 0x7E,
    0xF0, 0x00, 0x7E, 0xF1, 0x00, 0x7E, 0xF2, 0x00, 0x7E, 0xF3, 0x00, 0x7E,
    0xD4, 0xB1, 0x3A, 0x01, 0xB2, 0x3D, 0x01, 0xB3, 0x40, 0x01, 0xB4, 0x43,
    0x01, 0x00, 0x7E, 0xF4, 0x00, 0x7E, 0xF5, 0x00, 0x7E, 0xF6, 0x00, 0x7E,
    0xF7, 0x00, 0x7E, 0xD3, 0x00, 0x7E, 0xD6, 0x00, 0x41, 0xC2, 0x42, 0xC3,
    0x43, 0xC4, 0x44, 0xC5, 0x45, 0xC6, 0x00, 0x41, 0xDA, 0x42, 0xD9, 0x43,
    0xD7, 0x44, 0xD8, 0x48, 0xD2, 0x46, 0xD5, 0x20, 0x20, 0x49, 0xB3, 0x4D,
    0xB0, 0x6A, 0x2A, 0x6B, 0x2B, 0x6C, 0x2C, 0x6D, 0x2D, 0x6E, 0x2E, 0x6F,
    0x2F, 0x70, 0x30, 0x71, 0x31, 0x72, 0x32, 0x73, 0x33, 0x74, 0x34, 0x75,
    0x35, 0x76, 0x36, 0x77, 0x37, 0x78, 0x38, 0x79, 0x39, 0x58, 0x3D, 0x50,
    0xC2, 0x51, 0xC3, 0x52, 0xC4, 0x53, 0xC5, 0xB2, 0x99, 0x01, 0x5A, 0x0B,
    0x00, 0x50, 0xF0, 0x51, 0xF1, 0x52, 0xF2, 0x53, 0xF3, 0x00, 0x20, 0x20,
    0x49, 0xB3, 0x4D, 0xB0, 0x6A, 0x2A, 0x6B, 0x2B, 0x6C, 0x2C, 0x6D, 0x2D,
    0x6E, 0x2E, 0x6F, 0x2F, 0x70, 0x30, 0x71, 0x31, 0x72, 0x32, 0x73, 0x33,
    0x74, 0x34, 0x75, 0x35, 0x76, 0x36, 0x77, 0x37, 0x78, 0x38, 0x79, 0x39,
    0x58, 0x3D, 0x00
};

int Terminal::matchEscape(int ch)
{
    uint8_t kch;
    for (;;) {
        kch = pgm_read_byte(keymap + offset);
        if (!kch) {
            // No match at this level, so the escape sequence is invalid.
            break;
        } else if (kch & 0x80) {
            // Interior node.
            if ((kch & 0x7F) == ch) {
                // Interior node matches.  Go down one tree level.
                offset = ((int)(pgm_read_byte(keymap + offset + 1))) |
                        (((int)(pgm_read_byte(keymap + offset + 2))) << 8);
                return -1;
            }
            offset += 3;
        } else {
            // Leaf node.
            if (kch == (uint8_t)ch) {
                // We have found a match on a full escape sequence.
                return pgm_read_byte(keymap + offset + 1);
            }
            offset += 2;
        }
    }
    return -2;
}

void Terminal::telnetCommand(uint8_t type, uint8_t option)
{
    uint8_t buf[3];
    buf[0] = (uint8_t)TelnetDefs::IAC;
    buf[1] = type;
    buf[2] = option;
    _stream->write(buf, 3);
}