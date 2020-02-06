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

#ifndef TELNET_DEFS_h
#define TELNET_DEFS_h

// References:
//     https://tools.ietf.org/html/rfc854
//     http://www.iana.org/assignments/telnet-options/telnet-options.xhtml

namespace TelnetDefs
{

enum Command
{
    EndOfFile           = 236,  
    Suspend             = 237,  
    Abort               = 238,  
    EndOfRecord         = 239,  
    SubEnd              = 240,  
    NOP                 = 241,  
    DataMark            = 242,  
    Break               = 243,  
    Interrupt           = 244,  
    AbortOutput         = 245,  
    AreYouThere         = 246,  
    EraseChar           = 247,  
    EraseLine           = 248,  
    GoAhead             = 249,  
    SubStart            = 250,  
    WILL                = 251,  
    WONT                = 252,  
    DO                  = 253,  
    DONT                = 254,  
    IAC                 = 255   
};

enum Option
{
    Binary              = 0,    
    Echo                = 1,    
    Reconnection        = 2,    
    SuppressGoAhead     = 3,    
    ApproxMsgSize       = 4,    
    Status              = 5,    
    TimingMark          = 6,    
    RemoteTransmitEcho  = 7,    
    LineWidth           = 8,    
    PageSize            = 9,    
    CarriageReturn      = 10,   
    HorzTabStops        = 11,   
    HorzTabStopDisp     = 12,   
    FormFeed            = 13,   
    VertTabStops        = 14,   
    VertTabStopDisp     = 15,   
    LineFeed            = 16,   
    ExtendedASCII       = 17,   
    Logout              = 18,   
    ByteMacro           = 19,   
    DataEntryTerminal   = 20,   
    SUPDUP              = 21,   
    SUPDUPOutput        = 22,   
    SendLocation        = 23,   
    TerminalType        = 24,   
    EndOfRecordOption   = 25,   
    TACACSUserId        = 26,   
    OutputMarking       = 27,   
    TerminalLocation    = 28,   
    Telnet3270Regime    = 29,   
    X3Pad               = 30,   
    WindowSize          = 31,   
    Speed               = 32,   
    RemoteFlowControl   = 33,   
    Linemode            = 34,   
    XDisplay            = 35,   
    EnvironmentOld      = 36,   
    Authentication      = 37,   
    Encryption          = 38,   
    Environment         = 39,   
    TN3270E             = 40,   
    XAUTH               = 41,   
    Charset             = 42,   
    RemoteSerialPort    = 43,   
    ComPortControl      = 44,   
    SuppressLocalEcho   = 45,   
    StartTLS            = 46,   
    Kermit              = 47,   
    SendURL             = 48,   
    ForwardX            = 49,   
    Extended            = 255   
};

};

#endif