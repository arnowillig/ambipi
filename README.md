# ambipi
Raspberry Pi Ambilight Controller

This project aims to provide AmbiLight for big screens like beamer-projections on canvas.
My setup is 200cm x 115cm. I use two WS2812 strips with a length of 315 cm each.

### Layout (Strips A + B)

``` 
 A A A A A A A A A A A B
 A                     B
 A                     B
 A                     B
 A                     B
 A B B B B B B B B B B B
 C
 ``` 
* A: Strip A
* B: Strip B
* C: Connectors


* RPi GPIO Strip A: 18
* RPi GPIO Strio B: 13
* RPi DMA: 10
* PWM Frequency: 800000

