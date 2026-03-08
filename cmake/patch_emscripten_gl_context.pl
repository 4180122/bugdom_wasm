#!/usr/bin/env perl
# Patch Emscripten-generated JS: trigger moduleContextCreatedCallbacks when
# emscripten_webgl_make_context_current is used (SDL path). Browser.createContext
# never runs in that case, so GLImmediate.init and generateTempBuffers never run.
use strict;
use warnings;

my $file = $ARGV[0] or die "Usage: $0 <Bugdom.js>\n";
open my $fh, '<', $file or die "Cannot read $file: $!\n";
local $/;
my $content = <$fh>;
close $fh;

$content =~ s/(var _emscripten_webgl_make_context_current = \(contextHandle\) => \{\s*var success = GL\.makeContextCurrent\(contextHandle\);)\s*(return success \? 0 : -5;)/$1
      if (success && contextHandle && typeof GLImmediate != "undefined" && !GLImmediate.initted && typeof Browser != "undefined" && Browser.moduleContextCreatedCallbacks && Browser.moduleContextCreatedCallbacks.length) { Browser.useWebGL = true; Browser.moduleContextCreatedCallbacks.forEach(function(c){c();}); }
      $2/s
  or die "Patch failed: pattern not found in $file\n";

open $fh, '>', $file or die "Cannot write $file: $!\n";
print $fh $content;
close $fh;
