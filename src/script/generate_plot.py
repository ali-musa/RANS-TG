import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pylab as pl
from xml.dom import minidom
import os
import os.path
from mpltools import style


#plot settings
legend_pos = (0.5,-0.2)
style.use('ieee.transaction')
# pl.rcParams['lines.linewidth'] = 2
# pl.rcParams['font.weight']="large"
# pl.rcParams['legend.loc'] = 'best'
# pl.rcParams['legend.set_bbox_to_anchor'] = (1,0.5)
pl.rc('legend', loc='upper center')#, bbox_to_anchor=(1, 0.5))#, color='r')
# pl.rcParams['legend.fancybox']=True#, shadow=True
# pl.rcParams['legend.bbox_to_anchor']=(1, 0.5)
# pl.rcParams['legend.bbox_to_anchor']=(1, 0.5)
# pl.rcParams['bbox_to_anchor']=(1, 0.5)
# pl.legend(bbox_to_anchor=(1, 0.5))
markerslist=["o","v","^","s","*","D","p","<", ">", "H", "1", "2","3", "4"]
# markerslist=["o","o","v","v","^","^","s","s","*","*","D","D","p","p","<","<"]
# markerslist=["x","x","x","x","x"]
pl.rcParams['savefig.dpi']=300


def plot_graphs(log_path, plot_path, typ):
	global markerslist, chunk_size, file_size_distribution, queue_limit
	if typ =="reqs":
		log_path+="arct/"
	elif typ == "flows":
		log_path+="afct/"

	fig=pl.figure()
	local_marker_list = markerslist[:]
	loads_array=[]
	y_array=[]
	label_array=[]
	for file in sorted(os.listdir(log_path)):
		loads = []
		y = []
		with open(log_path+file, 'r') as csvfile:
			for line in csvfile:
				lineList = line.split(",")
				loads.append(lineList[0])
				y.append(lineList[1].split("\n")[0])

		lab = file.split(".")[0]
		pl.plot(loads, y, label=lab,marker=local_marker_list.pop(0))
		loads_array.append(loads)
		y_array.append(y)
		label_array.append(lab)




	lg = pl.legend(bbox_to_anchor=legend_pos)#loc='best', fancybox=True)#, shadow=True)
	lg.draw_frame(True)
	# lg = pl.legend(loc='center left', bbox_to_anchor=(1, 0.5))
	# lg.draw_frame(True)
	# pl.title("64MB chunks, 1Gbps links, 10 servers")	
	
	# pl.title(str(float(chunk_size)/1000000)+" MB\n"+file_size_distribution)
	# pl.text(-0.2,-0.2,"queue_limit: "+str(queue_limit), fontsize=8)
	pl.grid(True)
	# pl.yscale('log')
	

	# show the plot on the screen
	# pl.show()
	directory=plot_path
	if not os.path.exists(directory):
		os.mkdir( directory );


	pl.xlabel("Load (%)")
	pl.ylabel("Request Completion Time (s)")
	# pl.ylim([0.0008,0.0014])
		# pl.ylim([0.0008,0.005])
		# pl.ylim([0.0005,0.004])
	# pl.ylim([0.05,20])
	# pl.xlim([0,90])
		# # pl.yscale('log')
		# fig.savefig(directory+"/"+filename[:-4]+"_scaled.png", bbox_inches='tight', dpi=resolution, transparent=False)
		# pl.ylim([0.0,0.025])
		# pl.xlim([0.0,80])
		# pl.ylim([0,0.18])
	if typ =="reqs":
		fig.savefig(directory+"/arct.png", bbox_inches='tight', transparent=False)
	elif typ == "flows":
		fig.savefig(directory+"/afct.png", bbox_inches='tight', transparent=False)

	pl.cla()   # Clear axis
	pl.clf()   # Clear figure
	pl.close() # Close a figure window
	return loads_array, y_array, label_array


if __name__ == '__main__':
	if len(sys.argv) is not 4:
		print "usage: "+ sys.argv[0] + " <log dir> <plot dir> <flows or reqs>"
		sys.exit()

	plot_graphs(sys.argv[1],sys.argv[2], sys.argv[3])
	# for file in os.listdir(log_dir+"exp"+str(exp_nums[0])+"/analysis/averages"):
	# 	if not file.startswith("contention") and not file.startswith("flowContentions"):
	# 		# print file
	# 		a,b,c = plot_graphs(exp_nums,file)
	# 		if(len(exp_nums)>=2):
	# 			#plot gains
	# 			if (file.startswith("afct") or file.startswith("percentile")):
	# 				i=0
	# 				fig2=pl.figure()
	# 				local_marker_list = markerslist[:]
	# 				# pl.rc('axes', prop_cycle=(cycler('color', ['r', 'g', 'b', 'y','c', 'm', 'y', 'k']) +
	# 	   #                     cycler('linestyle', ['-', '--', ':', '-.'])))
	# 				for y in b[1:]:

	# 					i+=1
	# 					x= [((float(m)-float(n))/float(m))*100.0 for m, n in zip(b[0], y) if float(m) is not 0.0]
	# 					if i==1:
	# 						pl.plot(a[i],x,marker=local_marker_list.pop(0), visible=False) #dummy 
	# 					pl.plot(a[i], x, label=c[i],marker=local_marker_list.pop(0))
	# 					# if i==3:
	# 					# 	pl.plot(-1000,-1000,marker=local_marker_list.pop(0)) #dummy 

	# 					# print file
	# 					# print c[i]
	# 					# print x
	# 				# pl.title("64MB chunks, 1Gbps links, 10 servers")	
	# 				pl.ylim([-5,100])
	# 				# pl.ylim([0,20])
	# 				# pl.xlim([0,70])
	# 				pl.axhline(0, color='black', linestyle='--')

	# 				lg = pl.legend(bbox_to_anchor=legend_pos)#(loc='best', fancybox=True)#, shadow=True)
	# 				lg.draw_frame(True)
	# 				pl.title(str(float(chunk_size)/1000000)+" MB\n"+file_size_distribution)
	# 				# pl.text(-0.2,-0.2,"queue_limit: "+str(queue_limit), fontsize=8)


	# 				# lg = pl.legend(loc='center left', bbox_to_anchor=(1, 0.5))
	# 				# lg.draw_frame(True)

	# 				# pl.xlim([0,80])
	# 				# pl.ylim([0,50])
	# 				pl.xlabel("Load (%)")
	# 				pl.grid(True)
	# 				if (file.startswith("afct")):
	# 					pl.ylabel("Average Improvement (%)")
	# 				else:
	# 					pl.ylabel(file[-6:-4]+"th Percentile Improvement (%)")
	# 				fig2.savefig(plot_dir+"exp"+str(exp_nums)+"/"+file[:-4]+"_gains.png", bbox_inches='tight',  transparent=False)


	# # for exp_num in exp_nums:
	# 	# plot_flow_contentions(exp_num, exp_nums)
	# print "Experiment(s) "+str(exp_nums)+" plotting complete\n************************************\n"