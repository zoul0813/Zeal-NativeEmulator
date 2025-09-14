# Hayes Modem

## Basic Command Structure

All commands start with AT (“attention”).

Commands are case-insensitive (but usually uppercase).

Multiple commands can be strung together (e.g. AT&F&C1&D2).

Most commands end when you send a carriage return (\r).

## Common Hayes AT Commands

Dialing & Call Control

`ATD<number>` - Dial a number

`ATDT5551234` = Dial with Tone

`ATDP5551234` = Dial with Pulse

`ATH` - Hang up (on-hook)

`ATH0` = Hang up

`ATH1` = Go off-hook

## Answering

`ATA` - Answer an incoming call

## Modem Status

`ATZ` - Reset modem (use stored profile)

`AT&F` - Factory defaults

`ATI` - Display modem identification

`ATI0`, `ATI1…` - Variant-specific info

## Echo & Verbosity

`ATE0` - Disable local echo

`ATE1` - Enable local echo

`ATV0` - Numeric result codes (e.g. 0, 1, 2)

`ATV1` - Verbose result codes (e.g. OK, CONNECT)

## Flow Control & DCD/DTR Handling

`AT&C1` - DCD follows carrier detect

`AT&D2` - Drop DTR = hang up

## Registers (S-Registers)

Modems keep configuration in “S-registers” (like small numbered config slots).
Examples:

`ATS0=1` - Auto-answer after 1 ring

`ATS7=60` - Wait 60 seconds for carrier detect

`ATS2=43` - Escape character is + (ASCII 43)

## Result Codes

After a command, the modem responds with one of:

* `OK` - success
* `ERROR` - invalid command
* `CONNECT` - carrier established
* `NO CARRIER` - lost carrier / no connection
* `BUSY` - busy signal
* `NO DIALTONE` - no dial tone
