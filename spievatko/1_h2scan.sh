1. scan the book to _.png (autosaves to __###.png; search for some terminal scanning prg)

2. scantailor the pngs (no white margins; output in tifs; __###_{1L|2R}.tif)

3. to create white margin of size of a5, horizontally centre and vertically move from top by +0+264 (approx by .88"), run:

   $ ls _*.tif | awk 'BEGIN{ a=0 }{ printf "composite -gravity center -gravity north -geometry +0+500 %s blnk_600.tif %03d.tif\n", $0, a++ }' | bash   # output is named ###.tif, starting with 000.tif

4. convert *.tif to *.pdf (individual files)

   $ ls 0* 1* 2* | awk 'BEGIN{ a=0 }{ printf "convert %s %03d.pdf\n", $0, a++ }' | bash   # output is named ###.pdf

5. combine *.pdf to aio.pdf

   $ pdftk *.pdf cat output aio.pdf

6. build index

   $ pdftk input.pdf dump_data > dump_data.txt  # to export the dump_data from input.pdf (the output file must be a *.txt)

   $ pdftk input.pdf update_info_utf8 dump_data.txt output output.pdf  # to import dump_data in utf8 into input.pdf and save it as output.pdf