# ttf2woff
Backup from http://wizard.ae.krakow.pl/~jb/ttf2woff/ by Jan Bobrowski
___

Command line utility converts TrueType and OpenType fonts to the WOFF format. It also reads TTC collections and WOFF2 (experimental), as well as WOFF for recompression. Outputs WOFF and TTF.
___
```bash
ttf2woff [-v] [-O|-S] [-t type] [-X table]... input [output]
ttf2woff -i [-v] [-O|-S] [-X table]... [-m file] [-p file] file
ttf2woff [-l] input
  -i      in place modification
  -O      optimize (default unless signed)
  -S      don't optimize
  -t fmt  output format: woff, ttf
  -u num  font number in collection (TTC), 0-based
  -m xml  metadata
  -p priv private data
  -X tag  remove table
  -l      list tables
  -v      be verbose
Use `-' to indicate standard input/output.
Skip output for dry run.
```

By default, ttf2woff tries to find more compact representation of some font tables (with marginal gain, usually).
Download

Source: [ttf2woff-1.2.tar.gz](http://wizard.ae.krakow.pl/~jb/ttf2woff/ttf2woff-1.2.tar.gz) (2017-07-30)

Windows executable: [ttf2woff.exe](http://wizard.ae.krakow.pl/~jb/ttf2woff/ttf2woff.exe) (command line, 254KiB)

See also

    lsfont.html — List glyphs in font online,
    7-Segment digital font — calculator & digital clock font,
    blank font — full of empty glyphs,
    Zopfli at GitHub. 

MIME type of WOFF is application/font-woff.
