**1**.在``setStart``和``setGoal``设置Z值相同的情况下，规划的路径在Z轴方向也会发生波动，不过波动不大。

## DONE

#### 2019.8.30:

1.点云地图转换成八叉树，并在RVIZ上显示；

2.``path_planning.cpp``可以订阅``october_msgs``,规划处可以避开障碍物的全局路径；

![](assets/markdown-img-paste-2019083100314129.png)

3.显示path的是nav_msgs，显示trajectory可以安装下moveit-ros-visualization;

### 2019.8.31
turtlebot：

![](assets/markdown-img-paste-20190831111334456.png)

## TODO

#### 2019.8.30

1.利用``goalpoint_transformer.cpp``在rviz上发布start、goal消息；

2.规划路径的约束：

``1`` 路径与障碍物需要有重合部分；
``2`` 路径搜索范围自定义设置；
``3`` 如何建三维点云地图，WQ推荐Categorapher;
