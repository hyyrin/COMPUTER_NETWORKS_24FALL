# Log Checker
The main script is `log_test.py`. 

## Description
The tests here are PROBABLY NOT used in the real tests, but if you want to check that you have a set of plausible logs, YOU SHOULD PASS THOSE TESTS.  
The real autograde tests are in another file that is not given to students.

## Usage
```
$ python3 log_test.py <send_log.txt> <recv_log.txt> <agent_log.txt> <src_filepath> <dst_filepath>
```
+ The output results is colored by default. 
+ To check the output, you can use the following scritps (assume that you save the output as a file `op`):
```
$ cat op        # tty deals with colors natually
$ less -R op    # less cannot deal with colors without -R
```
+ You could also use vscode extensions, e.g. "ANSI Colors": command prompt "ANSI Text: Open Preview"
+ To remove colors / convert to pure text, use something like this:
```
$ python ...  | sed -r "s/\x1B\[([0-9]{1,3}(;[0-9]{1,2};?)?)?[mGK]//g"
```

