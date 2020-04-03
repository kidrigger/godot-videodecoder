extends Control

func _ready() -> void:
	var stream = VideoStreamGDNative.new()
	var file = $VideoPlayer.stream.get_file()
	print(file)
	stream.set_file(file)
	var vp = $VideoPlayer
	vp.stream = stream

	var sp = vp.stream_position
	# hack: to get the stream length, set the position to a negative number
	# the plugin will set the position to the end of the stream instead.
	vp.stream_position = -1
	var duration = vp.stream_position
	$ProgressBar.max_value = duration
	vp.stream_position = sp
	vp.play()

func _on_VideoPlayer_finished():
	get_tree().quit()

func _process(delta: float) -> void:
	var pos = $VideoPlayer.stream_position
	$ProgressBar.value = pos
	$ProgressBar/Label.text = '%.f / %.f seconds' % [pos, $ProgressBar.max_value]
