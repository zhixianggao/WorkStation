# -*- coding: utf-8 -*-

from selenium import webdriver
from selenium.webdriver import ActionChains
from selenium.webdriver.common.by import By
from selenium.webdriver.common.keys import Keys
from selenium.webdriver.support import expected_conditions as EC
from selenium.webdriver.support.ui import Select
from selenium.webdriver.support.ui import WebDriverWait
import logging, time, sys

def login(driver):
    found = 0

    try:
        WebDriverWait(driver, 10).until(
            EC.visibility_of_element_located((By.TAG_NAME, 'button'))
        )
    except:
        return False

    elements = driver.find_elements(By.TAG_NAME, 'input')
    for i in elements:
        elem_type = i.get_attribute('type')
        if elem_type == 'text':
            i.send_keys('SM1371')
            found |= 0x01
        elif elem_type == 'password':
            i.send_keys('sunmi123')
            found |= 0x02
    if found != 0x03:
        return False

    elements = driver.find_elements(By.TAG_NAME, 'button')
    for i in elements:
        elem_type = i.get_attribute('type')
        if elem_type == 'button':
            i.click()
            return True
    return False

def main():
    logging.basicConfig(filename='mgt.log', level=logging.INFO)

    channel = ''
    sn = ''

    if len(sys.argv) >= 2:
        channel = sys.argv[1]
        if len(sys.argv) >= 3:
            sn = sys.argv[2].upper()

    options = webdriver.ChromeOptions()
    options.add_experimental_option('excludeSwitches', ['enable-logging'])
    driver = webdriver.Chrome(options=options)

    logging.info('opening https://mgt.sunmi.com/...')
    driver.get('https://mgt.sunmi.com/')

    logging.info('logging in...')
    if not login(driver):
        sys.exit(1)

    logging.info('logged in.')
    try:
        WebDriverWait(driver, 10).until(
            EC.url_to_be('https://mgt.sunmi.com/')
        )
    except:
        sys.exit(1)

    logging.info('opening https://mgt.sunmi.com/wireless/printer...')
    driver.get('https://mgt.sunmi.com/wireless/printer')

    try:
        WebDriverWait(driver, 10).until(
            EC.visibility_of_element_located((By.ID, 'channel'))
        )
    except:
        sys.exit(1)

    logging.info('filling channel...')
    element = driver.find_element(By.ID, 'channel')
    if element is None:
        sys.exit(1)
    if channel != '':
        element.send_keys(channel)

    logging.info('filling sn...')
    element = driver.find_element(By.ID, 'sn')
    if element is None:
        sys.exit(1)
    if sn != '':
        element.send_keys(sn)

    logging.info('clicking button...')
    element = driver.find_element(By.TAG_NAME, 'button')
    if element is None:
        sys.exit(1)
    element.click()

    '''
    logging.info('waiting for page loading...')
    try:
        WebDriverWait(driver, 10).until(
            EC.visibility_of_element_located((By.CLASS_NAME, 'ant-pagination-item'))
        )
    except:
        sys.exit(1)
    '''

    while True:
        time.sleep(2)
        logging.info('jumping to the last page...')
        elements = driver.find_elements(By.CLASS_NAME, 'ant-pagination-item')
        elements.pop(-1).click()

        time.sleep(2)
        elements = driver.find_elements(By.CLASS_NAME, 'ant-dropdown-link')
        elm =  elements.pop(-1)
        if elm.text == '更多':
            elm.click()

        time.sleep(1)
        elements = driver.find_elements(By.XPATH, '//div/div/div/div[2]')
        for elm in elements:
            if elm.text == '设备LOG':
                elm.click()
                break

        time.sleep(1)
        element = driver.find_element(By.XPATH, '/html/body/div/div/main/div/div/div/div/div/div/div/div/div/div/div/span[2]')
        element.click()

        time.sleep(1)
        element = driver.find_element(By.XPATH, '//div/div/div/div/div/div/div/table/thead/tr/th[2]')
        element.click()
        time.sleep(1)
        element.click()

    time.sleep(10)
    return

    page = 1
    first_sn_of_last_page = ''

    logging.info('getting device information...')
    while True:
        logging.info('page = ' + str(page))
        try:
            WebDriverWait(driver, 10).until(
                EC.visibility_of_element_located((By.CLASS_NAME, 'ant-table-pagination'))
            )
            WebDriverWait(driver, 10).until(
                EC.visibility_of_element_located((By.CLASS_NAME, 'ant-table-row-level-0'))
            )
            WebDriverWait(driver, 10).until(
                EC.visibility_of_element_located((By.CLASS_NAME, 'ant-pagination-options-quick-jumper'))
            )
        except:
            logging.error('page not responding.')
            driver.navigate().refresh()
            continue

        element = driver.find_element(By.CLASS_NAME, 'ant-table-pagination')
        active_item = element.find_element(By.CLASS_NAME, 'ant-pagination-item-active')
        curr_page = active_item.get_attribute('title')
        if int(curr_page) != page:
            logging.warning('page not matched: ' + curr_page + ', ' + str(page))
            element = driver.find_element(By.CLASS_NAME, 'ant-pagination-options-quick-jumper')
            editbox = element.find_element(By.TAG_NAME, 'input')
            editbox.send_keys(str(page))
            editbox.send_keys(Keys.ENTER)
            continue

        try:
            retries = 20
            while retries > 0:
                time.sleep(0.5)
                retries -= 1
                elements = driver.find_elements(By.CLASS_NAME, 'ant-table-row-level-0')
                if len(elements) == 10:
                    fields = elements[0].find_elements(By.TAG_NAME, 'td')
                    if len(fields) == 9 and fields[2].text != first_sn_of_last_page:
                        first_sn_of_last_page = fields[2].text
                        break
            if retries == 0:
                logging.warning(str(len(elements)) + ', ' + str(len(fields)) + ', ' + fields[2].text + ', ' + first_sn_of_last_page)
                driver.navigate().refresh()
                continue
            for i in elements:
                fields = i.find_elements(By.TAG_NAME, 'td')
                if len(fields) == 9:
                    print('%d'%(page), sep='', end='')
                    for j in range(1, 4):
                        print(',%s'%(fields[j].text), sep='', end='')
                    print('')
        except:
            logging.error('exception 1.')
            continue

        element = driver.find_element(By.CLASS_NAME, 'ant-pagination-next')
        if element.get_attribute('aria-disabled') == 'true':
            break

        '''
        element = driver.find_element(By.CLASS_NAME, 'ant-pagination-next')
        if element.get_attribute('aria-disabled') == 'true':
            break
        ActionChains(driver).click(element).perform()
        '''
        page += 1
        element = driver.find_element(By.CLASS_NAME, 'ant-pagination-options-quick-jumper')
        editbox = element.find_element(By.TAG_NAME, 'input')
        editbox.send_keys(str(page))
        editbox.send_keys(Keys.ENTER)

    logging.info('done.')

    driver.close()

    driver.quit()

if __name__ == '__main__':
    main()
