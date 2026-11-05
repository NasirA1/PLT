namespace IDE
{
  partial class FrmMain
  {
    /// <summary>
    /// Required designer variable.
    /// </summary>
    private System.ComponentModel.IContainer components = null;

    /// <summary>
    /// Clean up any resources being used.
    /// </summary>
    /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
    protected override void Dispose(bool disposing)
    {
      if (disposing && (components != null))
      {
        components.Dispose();
      }
      base.Dispose(disposing);
    }

    #region Windows Form Designer generated code

    /// <summary>
    /// Required method for Designer support - do not modify
    /// the contents of this method with the code editor.
    /// </summary>
    private void InitializeComponent()
    {
      this.splitContainer1 = new System.Windows.Forms.SplitContainer();
      this.splitContainer2 = new System.Windows.Forms.SplitContainer();
      this.btnRun = new System.Windows.Forms.Button();
      this.btnCompile = new System.Windows.Forms.Button();
      this.rtfSource = new System.Windows.Forms.RichTextBox();
      this.rtfConsole = new System.Windows.Forms.RichTextBox();
      ((System.ComponentModel.ISupportInitialize)(this.splitContainer1)).BeginInit();
      this.splitContainer1.Panel1.SuspendLayout();
      this.splitContainer1.Panel2.SuspendLayout();
      this.splitContainer1.SuspendLayout();
      ((System.ComponentModel.ISupportInitialize)(this.splitContainer2)).BeginInit();
      this.splitContainer2.Panel1.SuspendLayout();
      this.splitContainer2.Panel2.SuspendLayout();
      this.splitContainer2.SuspendLayout();
      this.SuspendLayout();
      // 
      // splitContainer1
      // 
      this.splitContainer1.Dock = System.Windows.Forms.DockStyle.Fill;
      this.splitContainer1.Location = new System.Drawing.Point(0, 0);
      this.splitContainer1.Name = "splitContainer1";
      // 
      // splitContainer1.Panel1
      // 
      this.splitContainer1.Panel1.Controls.Add(this.splitContainer2);
      // 
      // splitContainer1.Panel2
      // 
      this.splitContainer1.Panel2.Controls.Add(this.rtfConsole);
      this.splitContainer1.Size = new System.Drawing.Size(784, 462);
      this.splitContainer1.SplitterDistance = 405;
      this.splitContainer1.TabIndex = 0;
      // 
      // splitContainer2
      // 
      this.splitContainer2.Dock = System.Windows.Forms.DockStyle.Fill;
      this.splitContainer2.Location = new System.Drawing.Point(0, 0);
      this.splitContainer2.Name = "splitContainer2";
      // 
      // splitContainer2.Panel1
      // 
      this.splitContainer2.Panel1.Controls.Add(this.btnRun);
      this.splitContainer2.Panel1.Controls.Add(this.btnCompile);
      // 
      // splitContainer2.Panel2
      // 
      this.splitContainer2.Panel2.Controls.Add(this.rtfSource);
      this.splitContainer2.Size = new System.Drawing.Size(405, 462);
      this.splitContainer2.SplitterDistance = 92;
      this.splitContainer2.TabIndex = 0;
      // 
      // btnRun
      // 
      this.btnRun.Location = new System.Drawing.Point(8, 37);
      this.btnRun.Name = "btnRun";
      this.btnRun.Size = new System.Drawing.Size(75, 23);
      this.btnRun.TabIndex = 1;
      this.btnRun.Text = "Run";
      this.btnRun.UseVisualStyleBackColor = true;
      this.btnRun.Click += new System.EventHandler(this.btnRun_Click);
      // 
      // btnCompile
      // 
      this.btnCompile.Location = new System.Drawing.Point(8, 8);
      this.btnCompile.Name = "btnCompile";
      this.btnCompile.Size = new System.Drawing.Size(75, 23);
      this.btnCompile.TabIndex = 0;
      this.btnCompile.Text = "Compile";
      this.btnCompile.UseVisualStyleBackColor = true;
      this.btnCompile.Click += new System.EventHandler(this.btnCompile_Click);
      // 
      // rtfSource
      // 
      this.rtfSource.Dock = System.Windows.Forms.DockStyle.Fill;
      this.rtfSource.Font = new System.Drawing.Font("Comic Sans MS", 14.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
      this.rtfSource.Location = new System.Drawing.Point(0, 0);
      this.rtfSource.Name = "rtfSource";
      this.rtfSource.Size = new System.Drawing.Size(309, 462);
      this.rtfSource.TabIndex = 1;
      this.rtfSource.Text = "";
      // 
      // rtfConsole
      // 
      this.rtfConsole.BackColor = System.Drawing.Color.Black;
      this.rtfConsole.Dock = System.Windows.Forms.DockStyle.Fill;
      this.rtfConsole.Font = new System.Drawing.Font("Lucida Console", 9.75F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
      this.rtfConsole.ForeColor = System.Drawing.Color.White;
      this.rtfConsole.Location = new System.Drawing.Point(0, 0);
      this.rtfConsole.Name = "rtfConsole";
      this.rtfConsole.ReadOnly = true;
      this.rtfConsole.Size = new System.Drawing.Size(375, 462);
      this.rtfConsole.TabIndex = 0;
      this.rtfConsole.Text = "";
      // 
      // FrmMain
      // 
      this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
      this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
      this.ClientSize = new System.Drawing.Size(784, 462);
      this.Controls.Add(this.splitContainer1);
      this.Name = "FrmMain";
      this.Text = "DLang IDE";
      this.Load += new System.EventHandler(this.FrmMain_Load);
      this.splitContainer1.Panel1.ResumeLayout(false);
      this.splitContainer1.Panel2.ResumeLayout(false);
      ((System.ComponentModel.ISupportInitialize)(this.splitContainer1)).EndInit();
      this.splitContainer1.ResumeLayout(false);
      this.splitContainer2.Panel1.ResumeLayout(false);
      this.splitContainer2.Panel2.ResumeLayout(false);
      ((System.ComponentModel.ISupportInitialize)(this.splitContainer2)).EndInit();
      this.splitContainer2.ResumeLayout(false);
      this.ResumeLayout(false);

    }

    #endregion

    private System.Windows.Forms.SplitContainer splitContainer1;
    private System.Windows.Forms.SplitContainer splitContainer2;
    private System.Windows.Forms.Button btnRun;
    private System.Windows.Forms.Button btnCompile;
    private System.Windows.Forms.RichTextBox rtfSource;
    private System.Windows.Forms.RichTextBox rtfConsole;

  }
}

