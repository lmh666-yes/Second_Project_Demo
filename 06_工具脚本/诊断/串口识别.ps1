[Console]::OutputEncoding=[Text.Encoding]::GetEncoding(936)
foreach($p in [System.IO.Ports.SerialPort]::GetPortNames()){
  try{
    $s=New-Object System.IO.Ports.SerialPort $p,115200,None,8,One
    $s.ReadTimeout=200; $s.Open(); Start-Sleep -Milliseconds 150; $s.DiscardInBuffer()
    $ms=New-Object System.IO.MemoryStream; $t0=Get-Date
    while(((Get-Date)-$t0).TotalSeconds -lt 3){
      $n=$s.BytesToRead
      if($n -gt 0){$b=New-Object byte[] $n; $r=$s.Read($b,0,$n); $ms.Write($b,0,$r)}
      Start-Sleep -Milliseconds 50
    }
    $s.Close()
    $t=[Text.Encoding]::GetEncoding(936).GetString($ms.ToArray())
    $kind='未知'
    if($t -match 'board1|TX_OK'){$kind='板1'}
    elseif($t -match 'MON|board2'){$kind='板2'}
    Write-Output ("$p : bytes=$($ms.Length) 识别=$kind")
    if($ms.Length -gt 0){ Write-Output ("    首行: " + ($t.Split([char]10)[0])) }
  } catch { Write-Output ("$p : 打不开 - $($_.Exception.Message)") }
}
