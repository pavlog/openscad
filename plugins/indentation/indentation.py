import time
import sys
#import pyastyle
class Unbuffered(object):
    def __init__(self, stream):
        self.stream = stream
    def write(self, data):
        self.stream.write(data)
        self.stream.flush()
    def __getattr__(self, attr):
        return getattr(self.stream, attr)
# required to disable buffering
sys.stdout = Unbuffered(sys.stdout)
sys.stdout.write("#Indentation plugin started")
#
#sys.stdout.write("AddMenuItem,menu_Edit\\---,after(editActionUnindent)\n");
sys.stdout.write("AddMenuItem,menu_Edit(&Edit)\\editActionReIndent(Re-Indent),after#editActionUnindent,Ctrl+Alt+I\n");

#time.sleep(5)  # Delay for some timex

#print("#Indentation plugin started")

while 1:
    for line in sys.stdin.readlines():
         sys.stdout.write(line);
    time.sleep(1);
         
#while True:
#		sys.stdout.write("#BBB\n");
#		time.sleep(5)  # Delay for some timex

#print("#2Indentation plugin started")
