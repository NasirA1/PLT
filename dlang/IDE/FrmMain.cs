using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace IDE
{

  public partial class FrmMain : Form
  {

    public FrmMain()
    {
      InitializeComponent();
    }


    private void FrmMain_Load(object sender, EventArgs e)
    {
      RichTextBox.CheckForIllegalCrossThreadCalls = false;
    }


    private void LaunchAndWait(string exe, string param)
    {
      using (Process process = new Process())
      {
        process.StartInfo.FileName = exe;
        process.StartInfo.Arguments = param;
        process.StartInfo.UseShellExecute = false;
        process.StartInfo.RedirectStandardOutput = true;
        process.StartInfo.RedirectStandardError = true;
        process.StartInfo.WindowStyle = ProcessWindowStyle.Hidden;
        process.StartInfo.CreateNoWindow = true;
        process.ErrorDataReceived += process_DataReceived;
        process.OutputDataReceived += process_DataReceived;
        
        process.Start();
        process.BeginOutputReadLine();
        process.WaitForExit();
        process.Close();
      }
    }


    void process_DataReceived(object sender, DataReceivedEventArgs e)
    {
      if (!String.IsNullOrEmpty(e.Data))
      {
        rtfConsole.AppendText(e.Data + Environment.NewLine);
      }
    }


    private void btnCompile_Click(object sender, EventArgs e)
    {
      ///if (File.Exists("out.d")) File.Delete("out.d");
      rtfConsole.Clear();
      //launch compiler passing it the source code
      LaunchAndWait("..\\..\\..\\Debug\\dlangc.exe", "\"" + rtfSource.Text + "\"");
      rtfConsole.ScrollToCaret();
    }


    private void btnRun_Click(object sender, EventArgs e)
    {
      //run the executable on the virtual machine
      rtfConsole.AppendText(Environment.NewLine);
      LaunchAndWait("..\\..\\..\\Debug\\dlang.exe", "out.d");
      rtfConsole.ScrollToCaret();
    }

  }

}
