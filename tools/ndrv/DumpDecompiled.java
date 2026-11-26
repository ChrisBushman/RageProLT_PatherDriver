import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
public class DumpDecompiled extends GhidraScript {
  public void run() throws Exception {
    String out = getScriptArgs()[0];
    DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
    PrintWriter pw = new PrintWriter(new FileWriter(out + "/funcs.c"));
    PrintWriter ix = new PrintWriter(new FileWriter(out + "/index.txt"));
    int n=0;
    for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
      DecompileResults r = di.decompileFunction(f, 60, monitor);
      long sz = f.getBody().getNumAddresses();
      ix.println(f.getEntryPoint() + " " + f.getName() + " size " + sz);
      if (r.getDecompiledFunction()!=null) { pw.println("/* ==== " + f.getName() + " @ " + f.getEntryPoint() + " size " + sz + " ==== */"); pw.println(r.getDecompiledFunction().getC()); }
      n++;
    }
    pw.close(); ix.close(); println("funcs " + n);
  }
}
