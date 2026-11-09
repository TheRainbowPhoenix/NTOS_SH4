import ghidra.app.script.GhidraScript;
public class SeedCeCommands extends GhidraScript {
 public void run() throws Exception {
  for(int i=0;i<25;i++){
   int p=getInt(toAddr(0x11394L+72*i));
   if(p>=0x11000&&p<0x1d000){disassemble(toAddr(p));if(getFunctionAt(toAddr(p))==null)createFunction(toAddr(p),"CeCommand_"+i);}
  }
 }
}
