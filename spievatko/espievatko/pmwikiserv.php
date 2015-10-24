<?php
/*  Copyright 2005 Patrick R. Michaud (pmichaud@pobox.com)
    This file is pmwikiserv.php; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published
    by the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.  
*/

# $PmWikiDir holds the location of the PmWiki "main" directory -- i.e.,
# the one that has pmwiki.php in it.  
$PmWikiDir = '..';

# $Listen identifies the ip address and port number on which to
# receive connections.  The default is 127.0.0.1, which limits
# incoming connections to the local host.  The value 0.0.0.0 is
# used to allow connections from any client.
# $Listen = "0.0.0.0:80";      # like a standard webserver
# $Listen = "0.0.0.0:8000";    # for servers w/o root access
$Listen = "127.0.0.1:80";

# $UrlHandler identifies which subroutine to call for each known
# url prefix.
$UrlHandler['/phpinfo'] = 'HandlePhpInfo';
$UrlHandler['/wiki'] = 'HandleWiki';
$UrlHandler['/pub'] = 'HandleFile';
$UrlHandler['/uploads'] = 'HandleFile';

# The $MimeType array maps file extensions to Content-Type headers.
$MimeType = array(
  'gif' => 'image/gif', 'jpg' => 'image/jpeg', 'jpeg' => 'image/jpeg',
  'png' => 'image/png', 'bmp' => 'image/bmp', 'ico' => 'image/x-icon',
  'wbmp' => 'image/vnd.wap.wbmp',
  'mp3' => 'audio/mpeg', 'au' => 'audio/basic', 'wav' => 'audio/x-wav',
  'mpg' => 'video/mpeg', 'mpeg' => 'video/mpeg',
  'mov' => 'video/quicktime', 'qt' => 'video/quicktime',
  'wmf' => 'text/plain', 'avi' => 'video/x-msvideo',
  'zip' => 'application/zip',
  'gz' => 'application/x-gzip', 'tgz' => 'application/x-gzip',
  'rpm' => 'application/x-rpm',
  'hqx' => 'application/mac-binhex40', 'sit' => 'application/x-stuffit',
  'doc' => 'application/msword', 'ppt' => 'application/vnd.ms-powerpoint',
  'xls' => 'application/vnd.ms-excel', 'mdb' => 'text/plain',
  'exe' => 'application/octet-stream',
  'pdf' => 'application/pdf', 'psd' => 'text/plain',
  'ps' => 'application/postscript', 'ai' => 'application/postscript',
  'eps' => 'application/postscript',
  'htm' => 'text/html', 'html' => 'text/html', 'css' => 'text/css',
  'fla' => 'application/x-shockwave-flash',
  'swf' => 'application/x-shockwave-flash',
  'txt' => 'text/plain', 'rtf' => 'application/rtf',
  'tex' => 'application/x-tex', 'dvi' => 'application/x-dvi',
  '' => 'text/plain');

# If you don't want to modify settings directly above, you can
# set the values in pmwikiserv.conf .
if (file_exists('pmwikiserv.conf')) include_once('pmwikiserv.conf');

# This kicks off the whole program.
MainServerLoop();
exit();

# MainServerLoop creates a socket for incoming connections,
# binds the socket, waits for an incoming connection, and then
# calls ProcessRequest() to process the connection.  Someday we'll
# turn this into a threaded server, but for the moment it can only
# handle one request at a time.

function MainServerLoop() {
  global $Listen, $AcceptSocket, $PmWikiDir;

  set_time_limit(0);
  chdir($PmWikiDir);

  list($ip, $port) = explode(':', $Listen, 2);

  if (!function_exists('socket_create')) dl('php_sockets.dll');
  $AcceptSocket = socket_create(AF_INET, SOCK_STREAM, 6);
  socket_bind($AcceptSocket, $ip, $port);
  socket_listen($AcceptSocket);
  print "Started...ready for requests\n";
  while (true) {
    $ClientSocket = socket_accept($AcceptSocket);
    ProcessRequest($ClientSocket);
    socket_close($ClientSocket);
  }
  socket_close($AcceptSocket);
}


# This function handles a single HTTP-request.  We start by
# reading the HTTP request headers from the client.  When we
# reach the end of the headers, we parse out the headers into
# environment variables, and then call the appropriate handler.
function ProcessRequest($ClientSocket) {
  global $UrlHandler;
  $req = '';
  while (!preg_match("/\r?\n\r?\n/",$req)) {
    if (socket_select($r=array($ClientSocket), $w=null, $x=null, 30) == 0)
      return;
    $req .= socket_read($ClientSocket, 2048);
  }
  list($req,$post) = preg_split("/\r?\n\r?\n/", $req, 2);
  $req = preg_split("/\r?\n/", $req);
  list($method, $path, $prot) = explode(' ', array_shift($req));
  putenv("REQUEST_METHOD=$method");
  putenv("REQUEST_URI=$path");
  list($script, $query) = explode('?', $path, 2);
  putenv("QUERY_STRING=$query");
  putenv("CONTENT_LENGTH=0");
  while ($req[0] > '') {
    list($h, $v) = explode(': ', array_shift($req), 2);
    $h = str_replace('-', '_', strtoupper($h));
    if (strncmp($h, 'CONTENT', 7)==0) { putenv("$h=$v"); continue; }
    putenv("HTTP_$h=$v");
  }
  foreach($UrlHandler as $p => $r) {
    if ($script == $p || 
        strncmp($script, "$p/", strlen($p)+1) == 0) {
      putenv("SCRIPT_NAME=$p");
      putenv("PATH_INFO=" . substr($script, strlen($p)));
      putenv("REDIRECT_STATUS=200");
      return $r($ClientSocket, $post);
    }
  }
  NotFound($ClientSocket, $script);
  ProcessResponse($ClientSocket, 
    "Status: 404\r\n\r\nUnable to find $script on this server.");
}

# ProcessResponse() takes care of sending the HTTP response back
# to the client.  It looks at the headers and body given by $out,
# adjusts any Status: lines in the stream, computes the Content-Length
# if one wasn't supplied, and sends the results back to the client.
function ProcessResponse($ClientSocket, $out) {
  if (is_array($out)) $out = implode('', $out);
  list($head, $body) = preg_split("/\r?\n\r?\n/", $out, 2);
  $head .= "\r\n";
  $status = '200';
  if (preg_match('/^Status: (\d+)/mi', $head, $match)) $status=$match[1];
  if (!preg_match('/^Content-Length: /mi', $head))
    $head .= "Content-Length: ".strlen($body)."\r\n";
  socket_write($ClientSocket, "HTTP/1.0 $status\r\n");
  socket_write($ClientSocket, $head);
  socket_write($ClientSocket, "\r\n");
  socket_write($ClientSocket, $body);
}

# NotFound() generates a basic 404 "Not Found" response.
function NotFound($ClientSocket, $what) {
  if ($what != '/favicon.ico') print "NotFound: $what\n";
  ProcessResponse($ClientSocket, 
    "Status: 404\r\n\r\nUnable to find $what on this server.");
}

# HandlePhpInfo() handles url paths that begin with /phpinfo.
function HandlePhpInfo($ClientSocket, $post) {
  ob_start();
  phpinfo();
  $out[] = "Content-type: text/html\n\n";
  $out[] = ob_get_clean();
  ProcessResponse($ClientSocket, $out);
}

# HandleWiki() handles the calls to pmwiki.php.  For each
# request it starts up a new instance of pmwiki.php.  Until
# pmwiki.php starts sending back data, we have to keep feeding it
# input data (since we might not have gotten it all while reading
# the headers).  However, once pmwiki.php is sending data, we
# can just read-read-read until we reach the end.
function HandleWiki($ClientSocket, $post) {
  global $PmWikiDir;
  $proc_talk = array( 0 => array("pipe", "r"), 1 => array("pipe", "w"));
  putenv("SCRIPT_FILENAME=pmwiki.php");
  $process = proc_open("php pmwiki.php", $proc_talk, $pipes);
  stream_set_blocking($pipes[1], 0);
  $len = getenv("CONTENT_LENGTH");
  while ($len - strlen($post) > 0) {
    $x = socket_read($ClientSocket, $len - strlen($post));
    if ($x === false) break;
    $post .= $x;
  }
  fwrite($pipes[0], $post);
  fclose($pipes[0]);
  stream_set_blocking($pipes[1], 1);
  while (!feof($pipes[1])) $out[] = fread($pipes[1], 1024);
  fclose($pipes[1]);
  proc_close($process);
  ProcessResponse($ClientSocket, $out);
}
  
# HandleFile() returns the contents of a file; generally these
# are requests made in the /pub or /uploads path.  For security
# reasons, we don't allow '..' in the path at all.
function HandleFile($ClientSocket, $req) {
  global $MimeType;
  $filename = './' . getenv('SCRIPT_NAME') . getenv('PATH_INFO');
  if (strstr($filename, '..') !== false) 
    return NotFound($ClientSocket, $filename);
  if (is_dir($filename)) return NotFound($ClientSocket, $filename);
  $c = file_get_contents($filename);
  if ($c === false) return NotFound($ClientSocket, $filename);
  $ext = preg_replace('/^.*\\./', '', $filename);
  $ctype = $MimeType[$ext];
  if (!$ctype) $ctype = 'application/octet-stream';
  ProcessResponse($ClientSocket, array("Content-Type: $ctype\n\n",$c));
}
  
    
