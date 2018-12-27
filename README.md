# Minishell
This is a small shell implemented in C.

## What It Can Do

* Pipelining
* Backgrounding
* STDIN and STDOUT redirection (with append)
* File name tab completion

## Installing

Simply run the make file that comes with the repo. This will create an executable called 'myshell' in the src directory.
```
$ make
```

## Examples

Syntax for using the shell features is just like bash.

### Redirection

```
echo 'hello world' > file.output
```

```
echo 'hello world again' >> file.output
```

```
cat < file.output
```

### Backgrounding

```
python3 slowscript.py &
```

### Pipelining

```
ps | grep bash
```
