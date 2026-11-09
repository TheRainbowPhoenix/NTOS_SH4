import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.*;
/** Export recognized functions as reference pseudocode, never build inputs.
 * First script argument: destination directory. Completion is asynchronous. */
public class ExportCommctrlC extends GhidraScript {
 public void run() throws Exception {
  final Program p=currentProgram;
  final File dir=new File(getScriptArgs()[0]);dir.mkdirs();
  final ArrayList<Function> functions=new ArrayList<Function>();
  FunctionIterator it=p.getFunctionManager().getFunctions(true);
  while(it.hasNext()){Function f=it.next();if(!f.isExternal())functions.add(f);}
  Thread worker=new Thread(()->{try{
   DecompInterface d=new DecompInterface();d.openProgram(p);
   PrintWriter index=new PrintWriter(new File(dir,"index.tsv"),"UTF-8");int ok=0;
   for(Function f:functions){
    DecompileResults r=d.decompileFunction(f,10,ghidra.util.task.TaskMonitor.DUMMY);
    boolean success=r.decompileCompleted()&&r.getDecompiledFunction()!=null;
    index.println(f.getEntryPoint()+"\t"+f.getName()+"\t"+success);index.flush();
    if(success){try(PrintWriter w=new PrintWriter(new File(dir,f.getEntryPoint()+".c"),"UTF-8")){
     w.println("/* Ghidra reference only: "+f.getName()+" @ "+f.getEntryPoint()+"; NOT a compilable port. */");
     w.print(r.getDecompiledFunction().getC());}ok++;}
    try(PrintWriter w=new PrintWriter(new File(dir,"progress.txt"))){w.println(ok+"/"+functions.size()+" current="+f.getEntryPoint());}
   }
   index.close();d.dispose();
   try(PrintWriter w=new PrintWriter(new File(dir,"complete.txt"))){w.println(ok+"/"+functions.size());}
  }catch(Exception e){try(PrintWriter w=new PrintWriter(new File(dir,"error.txt"))){e.printStackTrace(w);}catch(Exception ignored){}}},"commctrl-export");
  worker.setDaemon(true);worker.start();println("Export started: "+functions.size()+" functions -> "+dir);
 }
}
