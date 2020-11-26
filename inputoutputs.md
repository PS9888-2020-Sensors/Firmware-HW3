# Collector

Press and hold `BTN` on powerup to clear all files in the SD card root

## State: Waiting for peer
  - `LED1`: Short blinks

## State: Transferring data
  - `LED1`: Rapid blinks

# Node

## State: Waiting for GPS lock:
  - `LED1`: Slow blinks (period 2s, 50%)

## State: Idle (GPS lock, no comms)
  - `LED1`: Short blinks

## State: Transferring data
  - `LED1`: Rapid blinks

## State: Shutdown
  - press and hold `BTN` in any state to enter
  - ie safe to cut power, no more writes from collector task
  - `LED1`: On
