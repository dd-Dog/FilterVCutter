// FilterCutter.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>


AVFormatContext *inputContext;
AVFormatContext *outputContext;

int64_t lastReadPacketTime;

//avformat_open_input回调，超过timeout则返回-1，表示打开失败
static int interrupt_cb(void *ctx) {
	int timeout = 3;
	if (av_gettime() - lastReadPacketTime > timeout * 1000 * 1000) {
		return -1;
	}
	return 0;
}

int OpenInput(string inputUrl) {
	inputContext = avformat_alloc_context();
	lastReadPacketTime = av_gettime();
	//设置回调
	inputContext->interrupt_callback.callback = interrupt_cb;
	//打开输入流
	int ret = avformat_open_input(&inputContext, inputUrl.c_str(), nullptr, nullptr);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Input file open input failed!\n");
		return ret;
	}
	ret = avformat_find_stream_info(inputContext, nullptr);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Find input file stream info failed!\n");
	}
	else {
		av_log(NULL, AV_LOG_FATAL, "Open input file %s success!\n", inputUrl);
	}
	return ret;	
}

shared_ptr<AVPacket> ReadPacketFromSource() {
	shared_ptr<AVPacket> packet(static_cast<AVPacket*>(av_malloc(sizeof(AVPacket))),
		[&](AVPacket *p) {av_packet_free(&p); av_freep(&p); });
	av_init_packet(packet.get());
	lastReadPacketTime = av_gettime();
	int ret = av_read_frame(inputContext, packet.get());
	if (ret >= 0) {
		return packet;
	}
	else {
		return nullptr;
	}

}

int OpenOutput(string outUrl) {
	int ret = avformat_alloc_output_context2(&outputContext, nullptr, "mpegts", outUrl.c_str());
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "open output file failed\n");
		goto Error;
	}
	ret = avio_open2(&outputContext->pb, outUrl.c_str(), AVIO_FLAG_WRITE, nullptr, nullptr);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "open avio failed\n");
		goto Error;
	}
	for (int i = 0; i < inputContext->nb_streams; i++) {
		AVStream * stream = avformat_new_stream(outputContext, inputContext->streams[i]->codec->codec);
		ret = avcodec_copy_context(stream->codec, inputContext->streams[i]->codec);
		if (ret < 0) {
			av_log(NULL, AV_LOG_ERROR, "copy codec context fialed\n");
			goto Error;
		}
	}
	ret = avformat_write_header(outputContext, nullptr);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "format write header failed\n");
		goto Error;
	}
	av_log(NULL, AV_LOG_FATAL, "Open output file success\n");
	return ret;
Error:
	if (outputContext) {
		for (int i = 0; i < outputContext->nb_streams; i++) {
			avcodec_close(outputContext->streams[i]->codec);
		}
		avformat_close_input(&outputContext);
	}
	return ret;
}

void Init() {
	av_register_all();
	avfilter_register_all();
	avformat_network_init();
	av_log_set_level(AV_LOG_ERROR);
}

void CloseInput() {
	if (inputContext != nullptr) {
		avformat_close_input(&inputContext);
	}
}

void CloseOutput() {
	if (outputContext != nullptr) {
		for (int i = 0; i < outputContext->nb_streams; i++) {
			AVCodecContext *codecContext = outputContext->streams[i]->codec;
			avcodec_close(codecContext);
		}
		avformat_close_input(&outputContext);
	}
}

int WritePacket(shared_ptr<AVPacket> packet) {
	auto inputStream = inputContext->streams[packet->stream_index];
	auto outputStream = outputContext->streams[packet->stream_index];
	cout << "inputStream time_base.num=" << (inputStream->time_base.num)<<",den="<<inputStream->time_base.den<<endl;
	//cout << "outputStream time_base.num=" << (outputStream->time_base.num) << ",den=" << outputStream->time_base.den << endl;
	cout<<"pakcet.pts=" << packet->pts << ",pakcet.dts=" << packet->dts << endl;
	return av_interleaved_write_frame(outputContext, packet.get());
}

int main()
{
	//定义裁剪位置，1)按包个数来裁剪,对应时间与帧率有关；2)根据时间戳裁剪
	int startPacketNum = 200;	//开始裁剪位置
	int discardPacketNum = 200;	//裁剪的包个数
	int count = 0;

	//记录startPacketNum最后一个包的pts和dts，本例中的视频源pts和dts是不同的
	int64_t lastPacketPts = AV_NOPTS_VALUE;
	int64_t lastPacketDts = AV_NOPTS_VALUE;
	//时间戳间隔，不同的视频源可能不同，我的是3000
	int timerIntervel = 3000;

	int64_t lastPts = AV_NOPTS_VALUE;


	Init();
	//只支持视频流，不能带有音频流
	int ret = OpenInput("gaoxiao-v.mp4");//视频流,注意输入如果带有音频会失败，导致音频与视频不同步
	if (ret >= 0) {
		ret = OpenOutput("gaoxiao-v-caijian.mp4");
	}
	if (ret < 0) goto Error;


	while (true) {
		count++;
		cout << "count=" << count << endl;
		auto packet = ReadPacketFromSource();
		if (packet) {
			if (count <= startPacketNum || count > startPacketNum + discardPacketNum) {
				if (count >= startPacketNum + discardPacketNum) {
					//需要调整dts和pts,调整策略和视频源的pts和dts的规律有关，不是固定的
					packet->dts = -6000 + (count - 1 - discardPacketNum) * timerIntervel;

					if (count % 4 == 0) {
						packet->pts = packet->dts;
					}
					else if (count % 4 == 1) {
						packet->pts = packet->dts + 3000;
					}
					else if (count % 4 == 2) {
						packet->pts = packet->dts + 15000;
					}
					else if (count % 4 == 3) {
						packet->pts = packet->dts + 6000;
					}
				}

				ret = WritePacket(packet);
				if (ret < 0) {
					cout << "Write packet failed!" << endl;
				}
				else {
					cout << "Write packet success!" << endl;
				}
			}
		}
		else {
			break;
		}
	}
	
	cout << "cut file end\n" << endl;
Error:

	CloseInput();
	CloseOutput();
	while (true) {
		this_thread::sleep_for(chrono::seconds(100));
	}
	return 0;
}

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门提示: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
