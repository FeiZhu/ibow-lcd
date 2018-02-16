# iBoW-LCD: Appearance-based Loop Closure Detection using Incremental Bags of Binary Words

iBoW-LCD is a library that can be used to detect loop closures in a sequence of images. Instead of using the typical Bag-of-Words (BoW) scheme, iBoW-LCD makes use of an incremental Bags of Binary Words algorithm called [OBIndex2](http://github.com/emiliofidalgo/obindex2), allowing the use of binary descriptors to accelerate the image description process and avoiding the problems that classical BoW approaches present.

iBoW-LCD is released as a ROS package, and relies on OpenCV 3.x and Boost libraries as well as [OBIndex2](http://github.com/emiliofidalgo/obindex2) package. It can be used with any binary descriptor computed using the OpenCV format.

Note that iBoW-LCD is research code. The authors are not responsible for any errors it may contain. **Use it at your own risk!**

# Conditions of use

iBoW-LCD is distributed under the terms of the [GPL3 License](http://github.com/emiliofidalgo/ibow-lcd/blob/master/LICENSE).

# Related publication

The details of the algorithm are explained in the following publication:

**iBoW-LCD: An Appearance-based Loop Closure Detection Approach using Incremental Bags of Binary Words**<br/>
Emilio Garcia-Fidalgo and Alberto Ortiz<br/>
Submitted to IEEE RA-L and IROS 2018

# Installation

1. First of all, you have to install [OBIndex2](http://github.com/emiliofidalgo/obindex2) and its dependencies as a ROS package.

2. Clone the repository into your workspace:
  ```
  cd ~/your_workspace/src
  git clone http://github.com/emiliofidalgo/ibow-lcd.git
  ```

3. Compile the package using, as usual, the `catkin_make` command:
  ```
  cd ..
  catkin_make -DCMAKE_BUILD_TYPE=Release
  ```

4. Finally, you can run an example with:
  ```
  rosrun ibow-lcd demo /directory/of/images
  ```

# Usage

To see an example of how to use the loop closure detector, see the demo file `src/main.cc`.

# Contact

If you have problems or questions using this code, please contact the author (emilio.garcia@uib.es). [Feature requests](http://github.com/emiliofidalgo/ibow-lcd/issues) and [contributions](http://github.com/emiliofidalgo/ibow-lcd/pulls) are totally welcome.

