#!/usr/bin/env python3
from selenium import webdriver
import time


driver = webdriver.Chrome() #创建Chrome对象

driver.get('https://www.baidu.com') #get方式访问百度

time.sleep(20)

driver.quit() #使用完, 记得关闭浏览器, 不然chromedriver.exe进程为一直在内存中
