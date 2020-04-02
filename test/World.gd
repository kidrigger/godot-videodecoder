extends Control

func _ready() -> void:
	var stream = VideoStreamGDNative.new()
	var file = $VideoPlayer.stream.get_file()
	print(file)
	stream.set_file(file)
	$VideoPlayer.stream = stream

	$VideoPlayer.play()
	var sp = $VideoPlayer.stream_position
	$VideoPlayer.stream_position = 99
	var pos = $VideoPlayer.stream_position
	$VideoPlayer.stream_position = sp
	if pos > 0:
		$ProgressBar.max_value = pos
		$VideoPlayer.stream_position = 0
		print($VideoPlayer.stream.get_class())


func _on_VideoPlayer_finished():
	get_tree().quit()
	pass # Replace with function body.

func _process(delta: float) -> void:
	var pos = $VideoPlayer.stream_position
	$ProgressBar.value = pos
