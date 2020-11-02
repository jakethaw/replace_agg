
# replace_agg

[SQLite3](https://www.sqlite.org/) `Replace_Agg(const, find, replace)` extension function.

Three arguments are all strings: `const`, `find`, and `replace`. The result is also a string which is derived from `const` by replacing every occurrence of each `find` with its corresponding `replace`.

Notes:
1. If `const` is not constant, then the value is arbitrary as visited by SQLite.
2. If the same `find` value is specified more than once, then the `replace` value is arbitrary as visited by SQLite.
3. Longer matching `find` values take precedence over shorter values.

## Compile

To compile `replace_agg` with [gcc](https://gcc.gnu.org/) as a run-time loadable extension:

### UNIX-like
```bash
gcc -g -O3 -fPIC -shared replace_agg.c -o replace_agg.so
```

### Mac
```bash
gcc -g -O3 -fPIC -dynamiclib replace_agg.c -o replace_agg.dylib
```

### Windows
```bash
gcc -g -O3 -shared replace_agg.c -o replace_agg.dll
```
## Examples

### Input

```sql

.load replace_agg

CREATE TABLE x(find, replace);
INSERT INTO x VALUES 
('dog', 'cat'),
('dog', 'nope'),
('cat', 'mouse'),
('mouse', 'dog'),
('ouse', 'nope'),
('house', 'hello'),
('h', 'nope'),
('hello', 'world');

CREATE TABLE y("group", find, replace);
INSERT INTO y VALUES 
(1, 'm', 1),
(1, 'c', 2),
(2, 'm', 3),
(2, 'c', 4);

.headers on
.mode box


.print ---------------------------------
.print -- EXAMPLE 1
.print ---------------------------------

SELECT *
  FROM x;

SELECT Replace_Agg('dog cat mouse house hello', find, replace) "dog cat mouse house hello"
  FROM x;

.print ---------------------------------
.print -- EXAMPLE 2
.print ---------------------------------

SELECT *
  FROM y;

SELECT "group",
       Replace_Agg('y=mx+c', find, replace) "y=mx+c"
  FROM y
 GROUP BY "group";
```

### Output
```bash
---------------------------------
-- EXAMPLE 1
---------------------------------
┌───────┬─────────┐
│ find  │ replace │
├───────┼─────────┤
│ dog   │ cat     │
│ dog   │ nope    │
│ cat   │ mouse   │
│ mouse │ dog     │
│ ouse  │ nope    │
│ house │ hello   │
│ h     │ nope    │
│ hello │ world   │
└───────┴─────────┘
┌───────────────────────────┐
│ dog cat mouse house hello │
├───────────────────────────┤
│ cat mouse dog hello world │
└───────────────────────────┘
---------------------------------
-- EXAMPLE 2
---------------------------------
┌───────┬──────┬─────────┐
│ group │ find │ replace │
├───────┼──────┼─────────┤
│ 1     │ m    │ 1       │
│ 1     │ c    │ 2       │
│ 2     │ m    │ 3       │
│ 2     │ c    │ 4       │
└───────┴──────┴─────────┘
┌───────┬────────┐
│ group │ y=mx+c │
├───────┼────────┤
│ 1     │ y=1x+2 │
│ 2     │ y=3x+4 │
└───────┴────────┘
```