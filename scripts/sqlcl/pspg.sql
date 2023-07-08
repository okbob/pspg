script
var CommandRegistry = Java.type("oracle.dbtools.raptor.newscriptrunner.CommandRegistry");
var CommandListener =  Java.type("oracle.dbtools.raptor.newscriptrunner.CommandListener")

var Runtime = Java.type("java.lang.Runtime");
var Scanner  = Java.type("java.util.Scanner");
var System = Java.type("java.lang.System");

var FileOutputStream = Java.type("java.io.FileOutputStream");
var ByteArrayOutputStream = Java.type("java.io.ByteArrayOutputStream");
var BufferedOutputStream = Java.type("java.io.BufferedOutputStream");

var Files = Java.type("java.nio.file.Files");
var Path = Java.type("java.nio.file.Path");
var File = Java.type("java.io.File");

var cmd = {};

cmd.handle = function (conn,ctx,cmd) {
  if (cmd.getSql().startsWith("pspg ")) {
    var tty = System.getenv("TTY");
    if (tty == null) {
      print("\nIn order to use pspg to page results, make sure you pass in your tty as an environment variable.");
      print("For example, invoke sqlcl like this:\n");
      print("$ TTY=$(tty) sqlcl ...\n");

      return true;
    }

    var sql = cmd.getSql().substring(5);
    var tempPath = Files.createTempFile("sqlcl-result", ".data");
    try {
      var bout = new ByteArrayOutputStream();
      sqlcl.setOut(new BufferedOutputStream(bout));

      // Set this to a big value so that we do not get repeating headers
      ctx.putProperty("script.runner.setpagesize", 1000000);
      sqlcl.setConn(conn);
      sqlcl.setStmt(sql);
      sqlcl.run();

      var fileOutputStream = new FileOutputStream(tempPath.toFile());
      bout.writeTo(fileOutputStream);
      fileOutputStream.close();

      var pager = Runtime.getRuntime().exec(
        [ "sh", "-c", "pspg '" + tempPath.toString() + "' < " + tty + " > " + tty ]
      );
      pager.waitFor();
    } finally {
      Files.delete(tempPath);
    }

    // return TRUE to indicate the command was handled
    return true;
  } 

  // return FALSE to indicate the command was not handled
  // and other commandListeners will be asked to handle it
  return false;
}

cmd.begin = function (conn,ctx,cmd) {}
cmd.end = function (conn,ctx,cmd) {}

var pspgCommand = Java.extend(CommandListener, {
  handleEvent: cmd.handle,
  beginEvent: cmd.begin, 
  endEvent: cmd.end    
});

// Registering the new Command
CommandRegistry.addForAllStmtsListener(pspgCommand.class);
/

