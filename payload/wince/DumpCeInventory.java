import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;
public class DumpCeInventory extends GhidraScript {
 PrintWriter out;
 void data(Data d){int n=d.getNumComponents();if(n>0){for(int i=0;i<n;i++)data(d.getComponent(i));}else out.println("D\t"+d.getAddress()+"\t"+d.getLength()+"\t"+d.getDataType().getName());}
 public void run() throws Exception {File f=new File(getScriptArgs()[0],currentProgram.getName()+".tsv");f.getParentFile().mkdirs();out=new PrintWriter(f,"UTF-8");out.println("BASE\t"+currentProgram.getImageBase());InstructionIterator it=currentProgram.getListing().getInstructions(true);int count=0;
 while(it.hasNext()){Instruction i=it.next();out.println("I\t"+i.getAddress()+"\t"+i.getLength()+"\t"+i.toString());count++;String m=i.getMnemonicString();if((m.equalsIgnoreCase("mov.l")||m.equalsIgnoreCase("mov.w"))&&i.getNumOperands()>1){for(Object o:i.getOpObjects(0))if(o instanceof Address)out.println("L\t"+o+"\t"+(m.endsWith("l")?4:2));}}
 DataIterator ds=currentProgram.getListing().getDefinedData(true);while(ds.hasNext())data(ds.next());out.close();println("EXPORTED "+f+" instructions="+count);}
}

