/*****************************************************************************
 函 数 名  : PrinterDataCut
 功能描述  : 对送往打印机的数据进行切割处理
 输入参数  : 
 输出参数  : 无
 返 回 值  :
 调用函数  :
 被调函数  :

 修改历史      :
  1.日    期   : 2019年4月26日
    作    者   : GZX
    修改内容   : 新生成函数

*****************************************************************************/
int PrinterDataCut(void * orderContent, int dataToPrintLen)
{

	int iRet = -1;
	int i;
	unsigned int SendLoopConut, RestByte;

	int prtSta = 0;//打印机承载能力标志：0承载能力内，-1承载能力外
	static unsigned int secCount	= 0;//打印机承载能力查询时间（s）

	if(NULL == orderContent)
	{
		Util_LogWrite("PrinterDataCut", "line %d, iRet=%d, Invalid orderContent!!!\n", __LINE__, iRet);
		return iRet;
	}

	char *DataToPrint	=	(char *)orderContent;//数据类型转成字符串

	if(dataToPrintLen > 0)
	{
		SendLoopConut	=	dataToPrintLen / BYTE_SEND_PRINTER_ONCE;//循环发送计数
		RestByte		=	dataToPrintLen % BYTE_SEND_PRINTER_ONCE;//剩余字节计数
	}

	//数据量非BYTE_SEND_PRINTER_ONCE整数倍
	if(SendLoopConut > 0 && RestByte != 0)
	{
		//打印BYTE_SEND_PRINTER_ONCE整数倍数据
		for (i = 0; i < SendLoopConut; i++)
		{
			//FW_SysDelay(500);
			//sleep(2);
			//Util_LogWrite("PrinterDataCut", "line %d\n", __LINE__);
			prtSta = cmd_proc_test(DataToPrint,BYTE_SEND_PRINTER_ONCE);
			if(prtSta == -1)
			{
				Util_LogWrite("PrinterDataCut", "line %d, ring_buffer_get error!!!\n", __LINE__);
				return prtSta;
			}
			//Util_LogWrite("PrinterDataCut", "line %d\n", __LINE__);
			DataToPrint += BYTE_SEND_PRINTER_ONCE;
		}

		//打印剩余数据
		FW_SysDelay(500);
		prtSta = cmd_proc_test(DataToPrint, RestByte);
		if(prtSta == -1)
		{
			Util_LogWrite("PrinterDataCut", "line %d, ring_buffer_get error!!!\n", __LINE__);
			return prtSta;
		}
	}
	//数据量为BYTE_SEND_PRINTER_ONCE整数倍且倍数大于等于2
	else if(SendLoopConut >1 && RestByte == 0)
	{
		//打印SendLoopConut减一个BYTE_SEND_PRINTER_ONCE整数倍数据
		for (i = 0; i < SendLoopConut-1; i++)
		{
			sleep(2);
			prtSta = cmd_proc_test(DataToPrint,BYTE_SEND_PRINTER_ONCE);
			if(prtSta == -1)
			{
				Util_LogWrite("PrinterDataCut", "line %d, ring_buffer_get error!!!\n", __LINE__);
				return prtSta;
			}

			DataToPrint += BYTE_SEND_PRINTER_ONCE;
		}

		//打印剩余数据
		sleep(2);
		prtSta = cmd_proc_test(DataToPrint, BYTE_SEND_PRINTER_ONCE);
		if(prtSta == -1)
		{
			Util_LogWrite("PrinterDataCut", "line %d, ring_buffer_get error!!!\n", __LINE__);
			return prtSta;
		}
	}
	//数据量为BYTE_SEND_PRINTER_ONCE
	else if(SendLoopConut = 1)
	{

		sleep(2);
		prtSta = cmd_proc_test(DataToPrint, BYTE_SEND_PRINTER_ONCE);
		if(prtSta == -1)
		{
			Util_LogWrite("PrinterDataCut", "line %d, ring_buffer_get error!!!\n", __LINE__);
			return prtSta;
		}
	}
	//总数据量不足BYTE_SEND_PRINTER_ONCE，则直接发送RestByte
	else
	{

		sleep(2);
		prtSta = cmd_proc_test(DataToPrint, BYTE_SEND_PRINTER_ONCE);
		if(prtSta == -1)
		{
			Util_LogWrite("PrinterDataCut", "line %d, ring_buffer_get error!!!\n", __LINE__);
			return prtSta;
		}
	}

	return 0;
}
