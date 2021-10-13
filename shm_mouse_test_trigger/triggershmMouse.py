from shared_struct import Struct

print("Press x [ENTER] for x axis, y [ENTER] for y axis")
cpp_interface = Struct.from_key(454)
while True:
    myInput= input() 
    if myInput == "x":
        cpp_interface.y=0
        cpp_interface.x=55
        cpp_interface.changed = 1
    elif myInput == "y":
        cpp_interface.y=55
        cpp_interface.x=0
        cpp_interface.changed = 1
    else:
        print("Unknown Input")
        print("Press x [ENTER] for x axis, y [ENTER] for y axis")
          