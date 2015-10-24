<?php if (!defined('PmWiki')) exit();
/*
+----------------------------------------------------------------------+
| See cookbook/chordpro/README.txt for information.
| See cookbook/chordpro/LICENSE.txt for licence.
+----------------------------------------------------------------------+
| Copyright 2005 Jeremy H. Sproat, 2009 Simon Davis
| This program is free software; you can redistribute it and/or modify
| it under the terms of the GNU General Public License, Version 2, as
| published by the Free Software Foundation.
| http://www.gnu.org/copyleft/gpl.html
| This program is distributed in the hope that it will be useful,
| but WITHOUT ANY WARRANTY; without even the implied warranty of
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
| GNU General Public License for more details.
+----------------------------------------------------------------------+
*/
# Version date
$RecipeInfo['ChordPro']['Version'] = '2009-10-01';
$FmtPV['$ChordProVersion'] = $RecipeInfo['ChordPro']['Version']; // return version as a custom page variable
# 2009-10-01 Display Title, subtitle, and album when they are defined, add debug and version directives
# 2009-08-20 Set up CSS file, add Album, handle blank lines better, use h2 & h3 for title and subtitle
#            Processing changes to display blank lines            

# declare $ChordPro for (:if enabled ChordPro:) recipe installation check
global $ChordPro; $ChordPro = 1;

## Uncomment include line to add a custom page storage location 
## and some bundled wikipages.

#@include("chordpro/bundlepages.php");

## Add a custom markup
Markup( 'ChordPro', 
  'fulltext', "/\\|\\|:(.*?):\\|\\|/seim", 
  "Keep(ChordPro_Parse(explode(\"\n\",PSS('$1'))))" );
# s = dot matches all chars including newline
# e = backreference substitution by expression
# i = case insensitive
# m = multiline
# uses lazy evaluation, preserves leading and trailing white space
# Keep prevents PmWiki markup being applied
#
# TODO: how to disable <pre> blocks?  This needs to be preserved from
# the preformatted block (line begins with space) as well as the
# paragraph (empty line) and the tables (vertical bar).  Is this what 
# <:block> is used for?


/** Main ChordPro parser
 *   /param   $cho  An array of strings containing the ChordPro markup.
 *                  One string per line.  Don't worry about stripping
 *                  whitespace.
 *   /return  The HTML-formatted song wrapped in a <div> of class "song"
 */
function ChordPro_Parse($cho) {

// Initialise variables
  $debugon = false; # debug
  $retval = '';  
  $inchorus = false;
  $intab = false;
  $chordblocks = array();
  $textfont = '';
  $textsize = '';
  $songtitle = '';
  $songsubtitle = '';
  $songalbum = '';
  $chordfont = '';
  $chordsize = '';
  $linecount = count($cho);
  
  global $RecipeInfo; // for version directive

  # include the CSS file in the PmWiki page if required
  static $css_generated = false;                   // stylesheet not added to page
  if (! $css_generated) {
    global $HTMLHeaderFmt;                          // reference the PmWiki HTMLHeaderFMT array
    $HTMLHeaderFmt ['ChordPro'] = "<link rel='stylesheet' type='text/css' href='\$PubDirUrl/css/chordpro.css' />\n";
    $css_generated = true;                          // remember that we have added the stylesheet to the page
  }
  
  if( $linecount <= 1 ) {
    # TODO: debugging message, replace me with something intelligent
    #return '<p><h1>Error: line count = ' . $linecount . '!</h1></p>';
  }
  
  for( $i=0; $i<$linecount; $i++ ) {
    # iterate through the ChordPro lines
    $choline = $cho[$i];
    $matches = array();
    $directive = '';
    $directiveargument = '';
  
#   if( preg_match( '/\{([^:]+)(:(.+))?\}/', $choline, $matches ) ) {
    if( preg_match( '/\{([^:]+?)(:([^}]+?))?\}/', $choline, $matches ) ) {
      # get directive
      $directive = trim($matches[1]);
      if( count($matches) > 2 ) {
        $directiveargument = trim($matches[3]);
      } else {
        $directiveargument = '';
      }
    }
if( $debugon ) { # show line # line value, line len
  $retval .= "\n" . '<span class="songdebug">Parse: line=' . $i . ' "' . $choline . '" len=' . strlen($choline) . '</span><br/>' . "\n"; # debug
}
    if( $choline{0} == '#' ) {
      # strip out code comments
      continue;
      
    } else if( $intab ) {
      # we're in a tablature block
      # pass through text unprocessed until we see {end_of_tab} or {eot}
      if( strcasecmp($directive,'eot') && 
          strcasecmp($directive,'end_of_tab')
      ) { # i.e., neither token appears...
        $retval .= $choline;
        $cholineend = $choline{strlen($choline)-1};
        if( $cholineend != "\x0a" && $cholineend != "\x0d" ) {
          # only add EOL if there is none
          $retval .= "\n";
        }
      } else {
        # start processing text
        $retval .= '</div> <!-- end tablature block -->' . "\n";
        $intab = false;
      }
      
    } else {
      # we're not in a tablature block
        
      if( strlen($directive)>0 ) {
        # process directives
      
        if( !strcasecmp($directive,'sot') || 
            !strcasecmp($directive,'start_of_tab')
        ) {
          # it's a start of a tablature block
          if( !$intab ) {
            $retval .= '<div class="songtab">'; # omit "\n" to prevent blank line
            $intab = true;
          }
          
        } else if( !strcasecmp($directive,'define') ) {
          # it's a chord fingering definition line
          # TODO: enable when table formatting is taken care of
          #array_push( $chordblocks, 
          #  ChordPro_ExtractChordBlock($directiveargument) ); 
        
        } else if( !strcasecmp($directive,'rowname') ) {
          # it's a proprietary Songbird Chords directive
          # TODO: enable when table formatting is taken care of
          #array_push( $chordblocks, 
          #  '<br clear="both"/><div class="chordblockrowname songcomment">' . $directiveargument . '</div>' ); 
        
        } else if( !strcasecmp($directive,'textfont') ) {
          # it's a text font name directive
          $textfont = $directiveargument;
        
        } else if( !strcasecmp($directive,'textsize') ) {
          # it's a text font size directive
          $textsize = $directiveargument;
        
        } else if( !strcasecmp($directive,'chordfont') ) {
          # it's a chord notation font name directive
          $chordfont = $directiveargument;
        
        } else if( !strcasecmp($directive,'chordsize') ) {
          # it's a chord notation font size directive
          $chordsize = $directiveargument;
        
        } else if( !strcasecmp($directive,'t') || 
                   !strcasecmp($directive,'title') ) {
          # it's a song title directive
          $songtitle = $directiveargument;
          $retval .= '<h2 class="songtitle">' . $directiveargument . '</h2>' . "\n";

        } else if( !strcasecmp($directive,'st') || 
                   !strcasecmp($directive,'subtitle') ) {
          # it's a song subtitle directive
          $songsubtitle = $directiveargument;
          $retval .= '<h3 class="songsubtitle">' . $directiveargument . '</h3>' . "\n";

        } else if( !strcasecmp($directive,'a') || 
                   !strcasecmp($directive,'album') ) {
          # it's a song album directive
          $songalbum = $directiveargument;
          $retval .= '<div class="songalbum">' . $directiveargument . '</div>' . "\n";
        
        } else if( !strcasecmp($directive,'v') || 
                   !strcasecmp($directive,'version') ) {
          # it's a recipe version directive
          $retval .= '<span class="songversion">Version ' . $RecipeInfo['ChordPro']['Version'] . '</span>' . "\n";
        
        } else if( !strcasecmp($directive,'d') || 
                   !strcasecmp($directive,'debug') ) {
          # it's a debug directive
          $debugon = ! debugon;

        } else if( !strcasecmp($directive,'soc') || 
                   !strcasecmp($directive,'start_of_choir') || # ChordPack compatibility
                   !strcasecmp($directive,'start_of_chorus') ) {
          # it's the beginning of a chorus block
          if( ! $inchorus ) {
            $retval .= '<div class="songchorus">' . "\n";
            $inchorus = true;
          }
        
        } else if( !strcasecmp($directive,'eoc') || 
                   !strcasecmp($directive,'end_of_choir') || # ChordPack compatibility
                   !strcasecmp($directive,'end_of_chorus') ) {
          # it's the end of a chorus block
          if( $inchorus ) {
            $retval .= '</div> <!-- end chorus block -->' . "\n";
            $inchorus = false;
          }
        
        } else if( !strcasecmp($directive,'c') || 
                   !strcasecmp($directive,'comment') ) {
          # it's a comment line
          if( strlen($directiveargument) > 0 ) {
            $retval .= '<div class="songcomment"><span class="songcommentsimple">' . 
                       $directiveargument . '</span></div>' . "\n";
          }

        } else if( !strcasecmp($directive,'ci') || 
                   !strcasecmp($directive,'comment_italic') ) {
          # it's an italicized comment line
          if( strlen($directiveargument) > 0 ) {
            $retval .= '<div class="songcomment"><span class="songcommentitalic">' . 
                       $directiveargument . '</span></div>' . "\n";
          }

        } else if( !strcasecmp($directive,'cb') || 
                   !strcasecmp($directive,'comment_box') ) {
          # it's a boxed comment line
          if( strlen($directiveargument) > 0 ) {
            $retval .= '<div class="songcomment"><span class="songcommentbox">' . 
                       $directiveargument . '</span></div>' . "\n";
          }

        } else if(
            !strcasecmp($directive,'np') ||
            !strcasecmp($directive,'new_page') ||
            !strcasecmp($directive,'npp') ||
            !strcasecmp($directive,'new_physical_page')
        ) {
          $retval .= '<div class="songnewpage"></div>' . "\n";

        } else if(
            !strcasecmp($directive,'g') ||
            !strcasecmp($directive,'grid') ||
            !strcasecmp($directive,'ns') ||
            !strcasecmp($directive,'new_song') ||
            !strcasecmp($directive,'ng') ||
            !strcasecmp($directive,'no_grid') ||
            !strcasecmp($directive,'tuning')
        ) {
          # ignore these directives
 
        } else {
          # unknown directive
          # TODO: debugging message, replace with something intelligent
          $retval .= '<span class="songmessage">Unknown directive {' . $directive .
            (strlen($directiveargument)>0?':'.$directiveargument.'':'') .
            '}</span><br/>' . "\n";
          
        }
        
      # I originally wanted to use strpos() to check for the presence of
      # a [ char, intending to format only lyric lines with embedded
      # chords, but PHP's implementation is incredibly bone-headed.
      # If it finds the substring at position 0, it returns FALSE (ok,
      # it's a numerical 0 cast into a boolean FALSE, but come on...)
      # ...but in the end I decided to format all lyric lines this way      
      } else if( strlen(trim($choline)) > 0 ) {
        # format lyrics with embedded chords
        $retval .= ChordPro_ExtractLyricBlocks($choline,$chordfont,$chordsize,$debugon);
        
      } else {
        # pass through unprocessed unknown text format
        # tacking on a trailing <br/>\n was making PmWiki replace with
        # a <p><br/></p> block in weird places...
        $retval .= $choline;
      }
      
    }
    
  }

  # finished with parsing, let's slap the thing together
  
  # set up custom page variables
  #global $FmtPV;
  #$FmtPV['$SongTitle'] = $songtitle;
  #$FmtPV['$SongSubTitle'] = $songsubtitle;
  #$FmtPV['$SongAlbum'] = $songalbum;
  
  # insert chord blocks
  if( count($chordblocks) > 0 ) {
    $prefix = '<div class="chordblocks">' . "\n";
    foreach( $chordblocks as $block ) {
      $prefix .= $block . "\n";
    }
    $prefix .= '<br style="clear:both;" /></div>' . "\n";
    $retval = $prefix . $retval;
  }
  
  # insert custom font styles
  if ( strlen($textfont) > 0 || strlen($textsize) > 0 ) {
    $prefix = '<div style="' .
      (strlen($textfont)>0?'font-family:\''.$textfont.'\';':'') .
      (strlen($textsize)>0?'font-size:'.$textsize.'px;':'') .
      '">' . "\n";
    $suffix = '</div>' . "\n";
    $retval = $prefix . $retval . $suffix;
  }
  
  return '<div class="song">' . "\n" . $retval . '</div>';
}


/** ChordPro parser helper function - parse chord definition blocks
 *   /param   $defline  A string: the definition line minus the
 *                      "define:" keyword and surrounding braces
 *   /return  The HTML-formatted chord in a div of class "chordblock"
 */
function ChordPro_ExtractChordBlock($defline) {
  $retval = '';
  $chordblock = '';
  $matches = array();
  $stringarray = array();
  $chordname = '';
  $basefret = '';
  $highfret = 0;
  $stringset = '';
  $fret = '';
  $finger = '';
  $stringrows = array(
    '======',
    '||||||',
    '||||||',
    '||||||',
    '||||||',
    '||||||',
    '||||||',
    '||||||',
    '||||||',
    '||||||',
    '||||||',
    '||||||',
    '||||||'
  );
  
  if( preg_match( '/(\S+) +base-fret +([0-9]+) +frets? +(.+)/i',
      $defline, $matches ) ) {
    # looks like a valid define: directive
    $chordname = trim($matches[1]);
    $basefret = trim($matches[2]);
    $stringset = trim($matches[3]);
    if( $basefret != 0 ) {
      # top of graph is not fret 0, change notation
      $stringrows[0] = '------';
    }
    $highfret = 1;
    
    # split the guitar strings into an array
    $stringarray = preg_split( '/[ _=]+/i', $stringset );
    if( count($stringarray) > 1 ) {
      for( $i = 0; $i < count($stringarray); $i++ ) {
        $str = $stringarray[$i];
        
        if( !strcmp( $str, '-' ) ) {
          # no info for this string, ignore
          #           
        } else if( !strcasecmp( $str, 'x' ) ) {
          # muted string
          $str = 'x';
          $stringrows[0]{$i} = $str;
          
        } else if( !strcmp( $str, '0' ) || !strcasecmp( $str, 'o' ) ) {
          # open string
          $str = 'o'; # TODO: make this configurable          
          $stringrows[0]{$i} = $str;
          
        } else {
          # fret position and finger
          if( preg_match( '/([x0-9]+)([imrpt])?/i', $str, $matches ) ) {
            $fret = $matches[1];
            if( count($matches) > 2 ) {
              $finger = $matches[2];
            }
            $finger = 'o'; # TODO: make this configurable
            if( !strcasecmp($fret,'x') ) {
              $fret = 0;
              $finger = 'x';
            }
            # put the fingering notation into the graph
            $stringrows[$fret]{$i} = $finger;
            if( $fret > $highfret ) {
              $highfret = $fret;
            }
          } else {
            # TODO: debugging message, replace with something intelligent
            return '<div class="chordblock">Malformed str: ' .
              $str . ' within ' . $defline . '</div>';
          }
        }
        
      }
      
    } else {
      # TODO: debugging message, replace with something intelligent
      return '<div class="chordblock">Malformed stringset: ' .
        $stringset . ' within ' . $defline . '</div>';
    }
    
    # tack on base-fret if not at fret 0
    if( $basefret != 0 ) {
      $stringrows[1] .= ' ' . $basefret . 'fr.';
    }
    
    # merge rows into one string
    for( $i = 0; $i <= $highfret + 1; $i++ ) {
      $chordblock .= $stringrows[$i] . "\n";
    }
    
    # center chord name if not too long
    if( strlen($chordname) < 6 ) {
      $chordname = str_pad( $chordname, 6, ' ', STR_PAD_BOTH );
    }
    
  } else {
    # TODO: debugging message, replace with something intelligent
    return '<div class="chordblock">Malformed define: ' . $defline . '</div>';
  }
  
  # format for html
  $retval = '<div class="chordblock">' . $chordname . "\n" . $chordblock . '</div>';
  return $retval;
}


/** ChordPro parser helper function - extract and parse lyrics with embedded chord notation
 *   /param   $choline  A string: the chordpro lyric line with embedded chords
 *   /param   $chordfont  A string: the optional chord font.  Set to '' for default.
 *   /param   $chordsize  A string: the optional chord font size.  Set to '' to disable.
 *   /return  The HTML-formatted lyric line
 */
function ChordPro_ExtractLyricBlocks($choline,$chordfont,$chordsize,$debugon) {
  # Pull the chord notation from a line of lyrics and return a formatted table
  # ...each column is a segment of lyrics with its accompanying chord above it
  # ...in general, a new column is created when a chord appears

  # You [Em]stole my heart and [A]that's what [G]really [D]hurts [A] [B] [C]
  #   ...becomes...
  # |    |[Em]               |[A]         |[G]    |[D]   |[A] |[B] |[C] |
  # |You |stole my heart and |that's what |really |hurts |    |    |    |

  # Ugly css hack with chordfont and chordsize in here somewhere
  
  $retval = '';
  $choline = trim($choline);
  $inchord = false;
  $chords = array();
  $lyrics = array();
  $col = 0;
  array_push( $chords, '' );
  array_push( $lyrics, '' );
  for( $i = 0; $i < strlen($choline); $i++ ) {
    $ch = $choline{$i};
if( $debugon ) { # show each character as we process it, separated by "-"
  $retval .= '<span class="songdebug">-' . $ch . '</span>'; # debug
}
    if($ch=='[') {
      # at start of chord boundry
      if($inchord) {
        # this no-op intentionally left blank
      } else { # not inchord
        $inchord = true;
        $col++;
        array_push( $chords, '' );
        array_push( $lyrics, '' );
      }
    } else if($ch==']') {
      # at end of chord boundry
      if($inchord) {
        $chords[$col] .= ' '; # separate chords
        $inchord = false;
      } else { # not inchord
        # this no-op intentionally left blank
      }
    } else { 
      # not at chord boundry
      if($inchord) {
        $chords[$col] .= $ch;
      } else {
        $lyrics[$col] .= $ch;
      }      
    }
  }
if( $debugon ) { # show column number, string in hex, str len
  $retval .= "\n" . '<span class="songdebug">Lyric: type=' . $col . ' "' . bin2hex ($choline) . '" len=' . strlen($choline) . '</span><br/>' . "\n"; # debug
}
  $retval .= '<table border=0 cellpadding=0 cellspacing=0 class="songline">' . "\n";
  # generate chord line
  if( ! ($col == 0 && strlen($chords[0]) == 0) ) { # no chords in line
    $retval .= '<tr class="songlyricchord">';
    for( $i = 0; $i <= $col; $i++ ) {
      $retval .= '<td';
      if( strlen($chordfont)>0 || strlen($chordsize)>0 ) {
        $retval .= ' style="';
        if( strlen($chordfont)>0 ) {
          $retval .= 'font-family:\'' . $chordfont . '\';';
        }
        if( strlen($chordsize)>0 ) {
          $retval .= 'font-size:' . $chordsize . ';';
        }
        $retval .= '"';
      }
      $retval .= '>' . $chords[$i];
if( $debugon ) { # show block #, str len
  $retval .= '<span class="songdebug">blk=' . $i . ' len=' . strlen($chords[$i]) . '</span>'; # debug
}
      $retval .= '</td>';
    }
    $retval .= '</tr>' . "\n";
  }
  # generate lyrics line
  $retval .= '<tr class="songlyricline">';
  for( $i = 0; $i <= $col; $i++ ) {
    $retval .= '<td>' . $lyrics[$i];
if( $debugon ) { # show block #, str len
  $retval .= '<span class="songdebug">blk=' . $i . ' len=' . strlen($lyrics[$i]) . '</span>'; # debug
}
    $retval .= '</td>';
  }
  if( $col == 0 ) {
    $retval .= '<td>&nbsp;</td>'; # &nbsp; to force content so empty table row is displayed
  }
  $retval .= '</tr></table>' . "\n";
  return $retval;
} # ChordPro_ExtractLyricBlocks


/** ChordPro parser test function - read a file and parse it
 *   /param   $chofile  name of ChordPro file to read
 *   /return  The file as sent through the parser
 */
function ChordPro_ParseFile($chofile) {
  $handle = fopen($chofile, 'r');
  $cho = array();
  if($handle) {
    while(!feof($handle)) {
      array_push( $cho, fgets($handle) );
    }
    fclose($handle);
    return ChordPro_Parse($cho);
  } else {
    fclose($chofile);
    return '<p><h1>Error opening file!</h1></p>';
    return -1;
  }
} # ChordPro_ParseFile
