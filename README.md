# dgrep
A very old Regular Expression grep replacement I wrote in '93

Sharing for laughs.

```$ cat Examples/ex1
% dgrep -d '^([abc]+)\1$'

pattern:^([abc]+)\1$
-------------------------------

         Compiled RE

State:START_STATE(0), num transitions 1
	Next state:BEGIN_LINE(1)
State:BEGIN_LINE(1), num transitions 1
	Next state:START_CAPTURE(2)
State:START_CAPTURE(2), num transitions 1
	Next state:SET(3)
State:SET(3), num transitions 2
	Set:<abc>
	Next state:SET(4)
	Next state:END_CAPTURE(5)
State:SET(4), num transitions 2
	Set:<abc>
	Next state:SET(4)
	Next state:END_CAPTURE(5)
State:END_CAPTURE(5), num transitions 1
	Next state:BACKREFERENCE(6)
State:BACKREFERENCE(6), num transitions 1
	Next state:END_LINE(7)
State:END_LINE(7), num transitions 1
	Next state:ACCEPT_STATE(8)

-------------------------------
aa
1:** Accept **
	capture 1 [a]
abcabc
2:** Accept **
	capture 1 [abc]
abcab
3:** Reject **
```
